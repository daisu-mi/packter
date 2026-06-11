mod auth;
mod eve;
mod parse;
mod thmon;
mod wire;

use std::net::SocketAddr;
use std::sync::{Arc, Mutex};
use std::time::Instant;

use axum::{
    extract::ws::{Message, WebSocket, WebSocketUpgrade},
    extract::State,
    response::IntoResponse,
    routing::get,
    Router,
};
use tokio::net::UdpSocket;
use tokio::sync::{broadcast, mpsc};
use tower_http::services::ServeDir;

use auth::{AgentDirectory, Verdict};
use parse::{parse_datagram, Control, Parsed};
use thmon::{Thmon, ThmonConfig};
use wire::{control_to_json, encode_fly_frame, json_escape, Stored};

const BATCH_MS: u64 = 33;
const REWIND_MS: u64 = 5 * 60 * 1000; // broker-side ring: 5 minutes (spec 2026-06-10)
const RING_CAP: usize = 2_000_000;
const BACKFILL_CHUNK: usize = 4096;

struct Shared {
    epoch: Instant,
    ring: Mutex<std::collections::VecDeque<Stored>>,
    frames: broadcast::Sender<Arc<Message>>,
    record: Option<Mutex<std::fs::File>>,
    /// latest caption per board index (agent -A id), replayed to new clients
    board_labels: Mutex<std::collections::HashMap<u8, String>>,
    /// number of boards the viewer should arrange on its circle
    board_count: u8,
}

fn board_label_json(index: u8, label: &str) -> String {
    format!(r#"{{"t":"board","index":{index},"label":"{}"}}"#, json_escape(label))
}

impl Shared {
    fn now_ms(&self) -> u64 {
        self.epoch.elapsed().as_millis() as u64
    }

    fn emit_board_label(&self, index: u8, label: &str) {
        self.board_labels.lock().unwrap().insert(index, label.to_string());
        let _ = self.frames.send(Arc::new(Message::Text(board_label_json(index, label))));
    }

    fn emit_control(&self, c: &Control) {
        if let Some(rec) = &self.record {
            use std::io::Write;
            let mut f = rec.lock().unwrap();
            let _ = writeln!(f, r#"{{"ts":{},"ctrl":{}}}"#, self.now_ms(), control_to_json(c));
        }
        let _ = self.frames.send(Arc::new(Message::Text(control_to_json(c))));
    }
}

struct Config {
    web_dir: String,
    udp_port: u16,
    http_port: u16,
    forward: Option<SocketAddr>,
    record: Option<String>,
    thmon: Option<String>,
    eve: Option<String>,
    eve_board: u8,
    boards: Vec<BoardRule>,
    agents: AgentDirectory,
    board_count: Option<u8>,
}

/// N面配置: map a datagram source address to a viewer board index.
/// Rule forms: "192.168.1.5=2" (exact, v4/v6) or "10.0.0.0/8=1" (v4 CIDR).
#[derive(Clone, Debug)]
struct BoardRule {
    matcher: BoardMatch,
    board: u8,
}

#[derive(Clone, Debug)]
enum BoardMatch {
    Exact(std::net::IpAddr),
    Cidr4(u32, u8),
}

fn parse_board_rule(s: &str) -> Option<BoardRule> {
    let (addr, board) = s.rsplit_once('=')?;
    let board: u8 = board.trim().parse().ok()?;
    let addr = addr.trim();
    if let Some((net, plen)) = addr.split_once('/') {
        let net: std::net::Ipv4Addr = net.parse().ok()?;
        let plen: u8 = plen.parse().ok()?;
        if plen > 32 {
            return None;
        }
        return Some(BoardRule { matcher: BoardMatch::Cidr4(u32::from(net), plen), board });
    }
    let ip: std::net::IpAddr = addr.parse().ok()?;
    Some(BoardRule { matcher: BoardMatch::Exact(ip), board })
}

fn board_for(peer: std::net::IpAddr, rules: &[BoardRule]) -> u8 {
    for r in rules {
        match (&r.matcher, peer) {
            (BoardMatch::Exact(ip), p) if *ip == p => return r.board,
            (BoardMatch::Cidr4(net, plen), std::net::IpAddr::V4(p)) => {
                let mask = if *plen == 0 { 0 } else { u32::MAX << (32 - *plen) };
                if (u32::from(p) & mask) == (*net & mask) {
                    return r.board;
                }
            }
            _ => {}
        }
    }
    0
}

fn parse_args() -> Config {
    let mut cfg = Config {
        web_dir: "../web".to_string(),
        udp_port: 11300,
        http_port: 11380,
        forward: None,
        record: None,
        thmon: None,
        eve: None,
        eve_board: 0,
        boards: Vec::new(),
        agents: AgentDirectory::default(),
        board_count: None,
    };
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--udp" => cfg.udp_port = args.next().and_then(|v| v.parse().ok()).expect("--udp PORT"),
            "--http" => cfg.http_port = args.next().and_then(|v| v.parse().ok()).expect("--http PORT"),
            "--forward" => {
                cfg.forward = Some(args.next().and_then(|v| v.parse().ok()).expect("--forward IP:PORT"))
            }
            "--record" => cfg.record = Some(args.next().expect("--record FILE")),
            "--thmon" => cfg.thmon = Some(args.next().expect("--thmon CONF")),
            "--eve" => cfg.eve = Some(args.next().expect("--eve FILE")),
            "--eve-board" => {
                cfg.eve_board = args.next().and_then(|v| v.parse().ok()).expect("--eve-board N")
            }
            "--board" => {
                let spec = args.next().expect("--board <ip|cidr>=<index>");
                cfg.boards.push(parse_board_rule(&spec).expect("invalid --board rule"));
            }
            "--agent" => {
                let spec = args.next().expect("--agent <id>=<board>");
                let (id, board) = spec.split_once('=').expect("--agent <id>=<board>");
                cfg.agents.add_board(id.trim(), board.trim().parse().expect("board index"));
            }
            "--agent-key" => {
                let spec = args.next().expect("--agent-key <id>=<pskfile>");
                let (id, path) = spec.split_once('=').expect("--agent-key <id>=<pskfile>");
                let key = std::fs::read_to_string(path.trim()).expect("read psk file");
                let key = key.lines().next().unwrap_or("").trim();
                assert!(!key.is_empty(), "psk file is empty");
                cfg.agents.add_key(id.trim(), key.as_bytes());
            }
            "--require-auth" => cfg.agents.require_auth = true,
            "--boards" => {
                cfg.board_count = Some(args.next().and_then(|v| v.parse().ok()).expect("--boards N"));
            }
            "--help" | "-h" => {
                println!("usage: packter-broker [WEB_DIR] [--udp 11300] [--http 11380]");
                println!("       [--forward IP:PORT] [--record FILE] [--thmon packter.conf]");
                println!("       [--eve eve.json] [--eve-board N] [--board <ip|cidr>=<index>]...");
                println!("       [--agent <id>=<board>]... [--agent-key <id>=<pskfile>]... [--require-auth]");
                println!("       [--boards N]  (viewer arranges N boards evenly on a circle)");
                std::process::exit(0);
            }
            other => cfg.web_dir = other.to_string(),
        }
    }
    cfg
}

#[tokio::main]
async fn main() {
    let cfg = parse_args();
    let auth_keys = cfg.agents.has_keys();
    let auth_required = cfg.agents.require_auth;
    // board count: explicit --boards, else derived from the highest board
    // index referenced by --agent / --board rules (+1), at least 2
    let rules_max = cfg.boards.iter().map(|r| r.board).max().unwrap_or(0)
        .max(cfg.agents.max_board())
        .max(cfg.eve_board);
    let board_count = cfg.board_count.unwrap_or((rules_max + 1).max(2));
    let (frame_tx, _) = broadcast::channel::<Arc<Message>>(512);
    let (item_tx, item_rx) = mpsc::channel::<Parsed>(65536);

    let record = cfg.record.as_ref().map(|path| {
        Mutex::new(
            std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(path)
                .expect("open record file"),
        )
    });

    let thmon = cfg.thmon.as_ref().map(|path| {
        let tc = ThmonConfig::from_file(path).expect("read thmon config");
        if !tc.any_threshold() {
            println!("*** thmon: no TH_* threshold given, monitor will not alert ***");
        }
        Thmon::new(tc)
    });

    let shared = Arc::new(Shared {
        epoch: Instant::now(),
        ring: Mutex::new(std::collections::VecDeque::new()),
        frames: frame_tx,
        record,
        board_labels: Mutex::new(std::collections::HashMap::new()),
        board_count,
    });

    tokio::spawn(udp_ingest(item_tx.clone(), cfg.udp_port, cfg.forward, cfg.boards.clone(), cfg.agents));
    if let Some(path) = cfg.eve.clone() {
        tokio::spawn(eve::tail_eve(path, cfg.eve_board, item_tx.clone()));
    }
    tokio::spawn(batcher(item_rx, shared.clone(), thmon));

    let app = Router::new()
        .route("/ws", get(ws_handler))
        .fallback_service(ServeDir::new(&cfg.web_dir))
        .with_state(shared);

    let addr = SocketAddr::from(([0, 0, 0, 0], cfg.http_port));
    println!("packter-broker 3.0.0-alpha.3");
    println!("  legacy UDP ingest : 0.0.0.0:{}", cfg.udp_port);
    println!("  viewer + websocket: http://localhost:{}/  (ws: /ws)", cfg.http_port);
    println!("  serving web dir   : {}", cfg.web_dir);
    if let Some(f) = cfg.forward {
        println!("  legacy passthrough: forwarding raw datagrams to {f}");
    }
    if let Some(r) = &cfg.record {
        println!("  recording         : {r} (jsonl)");
    }
    if let Some(t) = &cfg.thmon {
        println!("  threshold monitor : {t}");
    }
    if let Some(e) = &cfg.eve {
        println!("  suricata eve      : tailing {e} (board {})", cfg.eve_board);
    }
    for r in &cfg.boards {
        println!("  board rule        : {:?} -> board {}", r.matcher, r.board);
    }
    if auth_keys || auth_required {
        println!("  agent auth        : keys={} require-auth={} (controls gated)",
                 auth_keys, auth_required);
    }
    println!("  boards            : {} (viewer arranges them on a circle)", board_count);
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind http");
    axum::serve(listener, app).await.expect("serve http");
}

async fn ws_handler(ws: WebSocketUpgrade, State(shared): State<Arc<Shared>>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| ws_client(socket, shared))
}

async fn ws_client(mut socket: WebSocket, shared: Arc<Shared>) {
    // subscribe before snapshotting so no live frame is missed
    let mut rx = shared.frames.subscribe();

    // tell the viewer how many boards to arrange on its circle
    if socket.send(Message::Text(format!(r#"{{"t":"layout","count":{}}}"#, shared.board_count)))
        .await.is_err() {
        return;
    }

    // replay current board captions so a late joiner sees agent labels
    let label_frames: Vec<String> = {
        let labels = shared.board_labels.lock().unwrap();
        labels.iter().map(|(idx, label)| board_label_json(*idx, label)).collect()
    };
    for json in label_frames {
        if socket.send(Message::Text(json)).await.is_err() {
            return;
        }
    }

    // backfill the broker-side rewind window for late joiners
    let backfill = {
        let now = shared.now_ms();
        let ring = shared.ring.lock().unwrap();
        let mut frames = Vec::new();
        let mut chunk: Vec<&Stored> = Vec::with_capacity(BACKFILL_CHUNK);
        for st in ring.iter() {
            chunk.push(st);
            if chunk.len() == BACKFILL_CHUNK {
                frames.push(encode_fly_frame(&chunk, now));
                chunk.clear();
            }
        }
        if !chunk.is_empty() {
            frames.push(encode_fly_frame(&chunk, now));
        }
        frames
    };
    for frame in backfill {
        if socket.send(Message::Binary(frame)).await.is_err() {
            return;
        }
    }

    loop {
        tokio::select! {
            frame = rx.recv() => {
                match frame {
                    Ok(msg) => {
                        if socket.send(msg.as_ref().clone()).await.is_err() {
                            break;
                        }
                    }
                    Err(broadcast::error::RecvError::Lagged(_)) => continue,
                    Err(broadcast::error::RecvError::Closed) => break,
                }
            }
            msg = socket.recv() => {
                match msg {
                    Some(Ok(_)) => continue,
                    _ => break,
                }
            }
        }
    }
}

async fn batcher(mut rx: mpsc::Receiver<Parsed>, shared: Arc<Shared>, mut thmon: Option<Thmon>) {
    let mut tick = tokio::time::interval(std::time::Duration::from_millis(BATCH_MS));
    let mut pending: Vec<Stored> = Vec::with_capacity(4096);
    loop {
        tokio::select! {
            _ = tick.tick() => {
                if !pending.is_empty() {
                    if shared.frames.receiver_count() > 0 {
                        let now = shared.now_ms();
                        let refs: Vec<&Stored> = pending.iter().collect();
                        let frame = encode_fly_frame(&refs, now);
                        let _ = shared.frames.send(Arc::new(Message::Binary(frame)));
                    }
                    if let Some(rec) = &shared.record {
                        use std::io::Write;
                        let mut f = rec.lock().unwrap();
                        for st in &pending {
                            let _ = writeln!(f,
                                r#"{{"ts":{},"kind":{},"sx":{},"sy":{},"dx":{},"dy":{},"flag":{},"desc":"{}"}}"#,
                                st.ts_ms, st.ev.kind, st.ev.sx, st.ev.sy, st.ev.dx, st.ev.dy,
                                st.ev.flag, json_escape(&st.ev.desc));
                        }
                    }
                    let mut ring = shared.ring.lock().unwrap();
                    let cutoff = shared.now_ms().saturating_sub(REWIND_MS);
                    for st in pending.drain(..) {
                        ring.push_back(st);
                    }
                    while ring.len() > RING_CAP
                        || ring.front().map(|s| s.ts_ms < cutoff).unwrap_or(false) {
                        ring.pop_front();
                    }
                }
            }
            item = rx.recv() => {
                match item {
                    Some(Parsed::Fly(ev)) => {
                        let now = shared.now_ms();
                        if let Some(th) = thmon.as_mut() {
                            for ctrl in th.on_flag(ev.flag, now) {
                                shared.emit_control(&ctrl);
                            }
                        }
                        pending.push(Stored { ts_ms: now, ev });
                    }
                    Some(Parsed::Ctrl(c)) => {
                        shared.emit_control(&c);
                    }
                    Some(Parsed::BoardLabel { index, label }) => {
                        shared.emit_board_label(index, &label);
                    }
                    None => break,
                }
            }
        }
    }
}

fn unix_now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

async fn udp_ingest(tx: mpsc::Sender<Parsed>, port: u16, forward: Option<SocketAddr>,
                    boards: Vec<BoardRule>, agents: AgentDirectory) {
    let sock = UdpSocket::bind(("0.0.0.0", port)).await.expect("bind udp");
    let fwd_sock = if forward.is_some() {
        Some(UdpSocket::bind(("0.0.0.0", 0)).await.expect("bind forward socket"))
    } else {
        None
    };
    let mut buf = vec![0u8; 65536];
    // last caption pushed per board, to emit a board-label frame only on change
    let mut last_labels: std::collections::HashMap<u8, String> = std::collections::HashMap::new();
    loop {
        let Ok((len, peer)) = sock.recv_from(&mut buf).await else { continue };
        let Ok(text) = std::str::from_utf8(&buf[..len]) else { continue };
        let peeled = match agents.peel(text, unix_now()) {
            Verdict::Accept(p) => p,
            Verdict::Reject(reason) => {
                eprintln!("auth: dropped datagram from {peer}: {reason}");
                continue;
            }
        };
        if let (Some(fs), Some(dst)) = (&fwd_sock, forward) {
            // legacy passthrough: forward without the PACKTERAGENT line
            let _ = fs.send_to(peeled.rest.as_bytes(), dst).await;
        }
        let board = peeled.board.unwrap_or_else(|| board_for(peer.ip(), &boards));
        let ctrl_ok = !agents.controls_need_auth() || peeled.authed;

        // an agent's -A id becomes its board caption (deduped on change)
        if let Some(id) = peeled.agent_id.as_ref() {
            if last_labels.get(&board).map(|s| s != id).unwrap_or(true) {
                last_labels.insert(board, id.clone());
                let _ = tx.try_send(Parsed::BoardLabel { index: board, label: id.clone() });
            }
        }

        for item in parse_datagram(peeled.rest) {
            match item {
                Parsed::Fly(mut ev) => {
                    ev.src_board = board;
                    let _ = tx.try_send(Parsed::Fly(ev));
                }
                Parsed::Ctrl(c) => {
                    if ctrl_ok {
                        let _ = tx.try_send(Parsed::Ctrl(c));
                    } else {
                        eprintln!("auth: control command from {peer} dropped (unauthenticated)");
                    }
                }
                Parsed::BoardLabel { .. } => {} // never produced by parse_datagram
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn board_rules() {
        let rules = vec![
            parse_board_rule("192.168.1.5=2").unwrap(),
            parse_board_rule("10.0.0.0/8=3").unwrap(),
            parse_board_rule("2001:db8::1=4").unwrap(),
        ];
        let ip = |s: &str| s.parse::<std::net::IpAddr>().unwrap();
        assert_eq!(board_for(ip("192.168.1.5"), &rules), 2);
        assert_eq!(board_for(ip("10.99.1.2"), &rules), 3);
        assert_eq!(board_for(ip("2001:db8::1"), &rules), 4);
        assert_eq!(board_for(ip("172.16.0.1"), &rules), 0); // default
        assert!(parse_board_rule("nonsense").is_none());
        assert!(parse_board_rule("10.0.0.0/40=1").is_none());
    }
}
