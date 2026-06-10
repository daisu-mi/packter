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
}

impl Shared {
    fn now_ms(&self) -> u64 {
        self.epoch.elapsed().as_millis() as u64
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
            "--help" | "-h" => {
                println!("usage: packter-broker [WEB_DIR] [--udp 11300] [--http 11380]");
                println!("       [--forward IP:PORT] [--record FILE] [--thmon packter.conf] [--eve eve.json]");
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
    });

    tokio::spawn(udp_ingest(item_tx.clone(), cfg.udp_port, cfg.forward));
    if let Some(path) = cfg.eve.clone() {
        tokio::spawn(eve::tail_eve(path, item_tx.clone()));
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
        println!("  suricata eve      : tailing {e}");
    }
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind http");
    axum::serve(listener, app).await.expect("serve http");
}

async fn ws_handler(ws: WebSocketUpgrade, State(shared): State<Arc<Shared>>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| ws_client(socket, shared))
}

async fn ws_client(mut socket: WebSocket, shared: Arc<Shared>) {
    // subscribe before snapshotting so no live frame is missed
    let mut rx = shared.frames.subscribe();

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
                    None => break,
                }
            }
        }
    }
}

async fn udp_ingest(tx: mpsc::Sender<Parsed>, port: u16, forward: Option<SocketAddr>) {
    let sock = UdpSocket::bind(("0.0.0.0", port)).await.expect("bind udp");
    let fwd_sock = if forward.is_some() {
        Some(UdpSocket::bind(("0.0.0.0", 0)).await.expect("bind forward socket"))
    } else {
        None
    };
    let mut buf = vec![0u8; 65536];
    loop {
        let Ok((len, _peer)) = sock.recv_from(&mut buf).await else { continue };
        if let (Some(fs), Some(dst)) = (&fwd_sock, forward) {
            let _ = fs.send_to(&buf[..len], dst).await;
        }
        let Ok(text) = std::str::from_utf8(&buf[..len]) else { continue };
        for item in parse_datagram(text) {
            let _ = tx.try_send(item);
        }
    }
}
