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

const BATCH_MS: u64 = 33;
const REWIND_MS: u64 = 5 * 60 * 1000; // broker-side ring: 5 minutes (spec 2026-06-10)
const RING_CAP: usize = 2_000_000;
const BACKFILL_CHUNK: usize = 4096;

const KIND_LAY: u8 = 0;
const KIND_BALLISTIC: u8 = 1;
const KIND_GATEWAY: u8 = 2;

/// One flying-object event (coordinates normalized 0..=1, legacy rules).
#[derive(Clone, Debug)]
struct FlyEvent {
    kind: u8,
    sx: f32,
    sy: f32,
    dx: f32,
    dy: f32,
    flag: u32,
    desc: String,
}

/// Non-fly command forwarded to viewers as a JSON text frame.
#[derive(Clone, Debug)]
enum Control {
    Msg { pic: String, html: String },
    Html { html: String },
    Se { file: String },
    Sound { time: String, file: String },
    Voice { text: String },
    Skydome { file: String },
}

enum Parsed {
    Fly(FlyEvent),
    Ctrl(Control),
}

struct Stored {
    ts_ms: u64,
    ev: FlyEvent,
}

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
}

struct Config {
    web_dir: String,
    udp_port: u16,
    http_port: u16,
    forward: Option<SocketAddr>,
    record: Option<String>,
}

fn parse_args() -> Config {
    let mut cfg = Config {
        web_dir: "../web".to_string(),
        udp_port: 11300,
        http_port: 11380,
        forward: None,
        record: None,
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
            "--help" | "-h" => {
                println!("usage: packter-broker [WEB_DIR] [--udp 11300] [--http 11380] [--forward IP:PORT] [--record FILE]");
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

    let shared = Arc::new(Shared {
        epoch: Instant::now(),
        ring: Mutex::new(std::collections::VecDeque::new()),
        frames: frame_tx,
        record,
    });

    tokio::spawn(udp_ingest(item_tx, cfg.udp_port, cfg.forward));
    tokio::spawn(batcher(item_rx, shared.clone()));

    let app = Router::new()
        .route("/ws", get(ws_handler))
        .fallback_service(ServeDir::new(&cfg.web_dir))
        .with_state(shared);

    let addr = SocketAddr::from(([0, 0, 0, 0], cfg.http_port));
    println!("packter-broker 3.0.0-alpha.2");
    println!("  legacy UDP ingest : 0.0.0.0:{}", cfg.udp_port);
    println!("  viewer + websocket: http://localhost:{}/  (ws: /ws)", cfg.http_port);
    println!("  serving web dir   : {}", cfg.web_dir);
    if let Some(f) = cfg.forward {
        println!("  legacy passthrough: forwarding raw datagrams to {f}");
    }
    if let Some(r) = &cfg.record {
        println!("  recording         : {r} (jsonl)");
    }
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind http");
    axum::serve(listener, app).await.expect("serve http");
}

async fn ws_handler(ws: WebSocketUpgrade, State(shared): State<Arc<Shared>>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| ws_client(socket, shared))
}

async fn ws_client(mut socket: WebSocket, shared: Arc<Shared>) {
    // subscribe before snapshotting so no live frame is missed; a few
    // duplicated events at the seam are harmless for visualization
    let mut rx = shared.frames.subscribe();

    // backfill: replay the broker-side ring so late joiners see the
    // rewind window immediately
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

/// Binary fly frame v2 (little endian):
///   u8 ver=2, u8 type=1, u16 reserved, u32 count, then per event:
///   i32 ageMs, f32 sx, f32 sy, f32 dx, f32 dy, u16 flag, u8 kind,
///   u8 descLen, descLen bytes UTF-8
fn encode_fly_frame(events: &[&Stored], now_ms: u64) -> Vec<u8> {
    let mut buf = Vec::with_capacity(8 + events.len() * 32);
    buf.extend_from_slice(&[2u8, 1u8, 0, 0]);
    buf.extend_from_slice(&(events.len() as u32).to_le_bytes());
    for st in events {
        let age = now_ms.saturating_sub(st.ts_ms) as i32;
        let desc = st.ev.desc.as_bytes();
        let dlen = desc.len().min(255);
        buf.extend_from_slice(&age.to_le_bytes());
        buf.extend_from_slice(&st.ev.sx.to_le_bytes());
        buf.extend_from_slice(&st.ev.sy.to_le_bytes());
        buf.extend_from_slice(&st.ev.dx.to_le_bytes());
        buf.extend_from_slice(&st.ev.dy.to_le_bytes());
        buf.extend_from_slice(&(st.ev.flag as u16).to_le_bytes());
        buf.push(st.ev.kind);
        buf.push(dlen as u8);
        buf.extend_from_slice(&desc[..dlen]);
    }
    buf
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn control_to_json(c: &Control) -> String {
    match c {
        Control::Msg { pic, html } => format!(
            r#"{{"t":"msg","pic":"{}","html":"{}"}}"#, json_escape(pic), json_escape(html)),
        Control::Html { html } => format!(r#"{{"t":"html","html":"{}"}}"#, json_escape(html)),
        Control::Se { file } => format!(r#"{{"t":"se","file":"{}"}}"#, json_escape(file)),
        Control::Sound { time, file } => format!(
            r#"{{"t":"sound","time":"{}","file":"{}"}}"#, json_escape(time), json_escape(file)),
        Control::Voice { text } => format!(r#"{{"t":"voice","text":"{}"}}"#, json_escape(text)),
        Control::Skydome { file } => format!(r#"{{"t":"skydome","file":"{}"}}"#, json_escape(file)),
    }
}

async fn batcher(mut rx: mpsc::Receiver<Parsed>, shared: Arc<Shared>) {
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
                        pending.push(Stored { ts_ms: shared.now_ms(), ev });
                    }
                    Some(Parsed::Ctrl(c)) => {
                        if let Some(rec) = &shared.record {
                            use std::io::Write;
                            let mut f = rec.lock().unwrap();
                            let _ = writeln!(f, r#"{{"ts":{},"ctrl":{}}}"#,
                                             shared.now_ms(), control_to_json(&c));
                        }
                        let _ = shared.frames.send(Arc::new(Message::Text(control_to_json(&c))));
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

/// Lenient line-oriented parser for the legacy Packter text protocol.
/// Fly headers start record mode; control headers consume the rest of
/// the datagram as their payload (2.5 viewer semantics: split('\n', 2)).
fn parse_datagram(text: &str) -> Vec<Parsed> {
    let mut items = Vec::new();
    let mut in_fly = false;
    let mut fly_kind = KIND_LAY;
    let mut rest = text;

    while !rest.is_empty() {
        let (line, after) = match rest.split_once('\n') {
            Some((l, a)) => (l.trim_end_matches('\r'), a),
            None => (rest.trim_end_matches('\r'), ""),
        };

        if line.is_empty() {
            rest = after;
            continue;
        }

        if let Some(kind) = fly_header(line) {
            in_fly = true;
            fly_kind = kind;
            rest = after;
            continue;
        }
        // "PACKTER rec" single-line form
        if let Some((head, tail)) = line.split_once(char::is_whitespace) {
            if let Some(kind) = fly_header(head) {
                in_fly = true;
                fly_kind = kind;
                if let Some(ev) = parse_record(tail, kind) {
                    items.push(Parsed::Fly(ev));
                }
                rest = after;
                continue;
            }
        }
        if let Some(ctrl) = control_header(line, after) {
            items.push(Parsed::Ctrl(ctrl));
            return items; // control payload consumes the rest of datagram
        }
        if line.starts_with("PACKTER") || line.starts_with("PACTER") {
            // unknown PACKTER* command: leave fly mode, skip
            in_fly = false;
            rest = after;
            continue;
        }
        if in_fly {
            if let Some(ev) = parse_record(line, fly_kind) {
                items.push(Parsed::Fly(ev));
            }
        }
        rest = after;
    }
    items
}

fn fly_header(s: &str) -> Option<u8> {
    match s {
        "PACKTER" | "PACTER" => Some(KIND_LAY),
        "PACKTERBALLISTIC" => Some(KIND_BALLISTIC),
        "PACKTERWITHGATEWAY" => Some(KIND_GATEWAY),
        _ => None,
    }
}

fn control_header(line: &str, payload: &str) -> Option<Control> {
    let payload = payload.trim_end_matches('\n').trim_end_matches('\r');
    match line {
        "PACKTERMSG" => {
            let (pic, html) = payload.split_once(',').unwrap_or((payload, ""));
            Some(Control::Msg { pic: pic.to_string(), html: html.to_string() })
        }
        "PACKTERHTML" => Some(Control::Html { html: payload.to_string() }),
        "PACKTERSE" => Some(Control::Se { file: payload.to_string() }),
        "PACKTERSOUND" => {
            let (time, file) = payload.split_once(',').unwrap_or(("0", payload));
            Some(Control::Sound { time: time.to_string(), file: file.to_string() })
        }
        "PACKTERVOICE" => Some(Control::Voice { text: payload.to_string() }),
        "PACKTERSKYDOMETEXTURE" => Some(Control::Skydome { file: payload.to_string() }),
        _ => None,
    }
}

/// record = SRCIP,DSTIP,SRCPORT,DSTPORT,FLAG[,DESCRIPTION]
fn parse_record(line: &str, kind: u8) -> Option<FlyEvent> {
    let parts: Vec<&str> = line.splitn(6, ',').collect();
    if parts.len() < 5 {
        return None;
    }
    let sx = addr_to_unit(parts[0])?;
    let dx = addr_to_unit(parts[1])?;
    let sy = port_to_unit(parts[2])?;
    let dy = port_to_unit(parts[3])?;
    let flag: u32 = parts[4].trim().parse().ok()?;
    let desc = parts.get(5).map(|s| s.to_string()).unwrap_or_default();
    Some(FlyEvent { kind, sx, sy, dx, dy, flag, desc })
}

fn addr_to_unit(s: &str) -> Option<f32> {
    let s = s.trim();
    if s.is_empty() {
        return Some(0.5);
    }
    if let Ok(ip) = s.parse::<std::net::Ipv4Addr>() {
        return Some(u32::from(ip) as f32 / u32::MAX as f32);
    }
    if let Ok(ip6) = s.parse::<std::net::Ipv6Addr>() {
        let seg = ip6.segments();
        let hi = ((seg[0] as u64) << 48) | ((seg[1] as u64) << 32)
               | ((seg[2] as u64) << 16) | seg[3] as u64;
        return Some((hi as f64 / u64::MAX as f64) as f32);
    }
    numeric_to_unit(s)
}

fn port_to_unit(s: &str) -> Option<f32> {
    let s = s.trim();
    if s.is_empty() {
        return Some(0.5);
    }
    numeric_to_unit(s)
}

/// Legacy String2Double: float 0..=1 stays, integer 1..=65536 divides by 65536.
fn numeric_to_unit(s: &str) -> Option<f32> {
    let v: f64 = s.parse().ok()?;
    if (1.0..=65536.0).contains(&v) && !s.contains('.') {
        return Some((v / 65536.0) as f32);
    }
    if (0.0..=1.0).contains(&v) {
        return Some(v as f32);
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    fn flys(items: &[Parsed]) -> Vec<&FlyEvent> {
        items.iter().filter_map(|i| match i {
            Parsed::Fly(e) => Some(e),
            _ => None,
        }).collect()
    }

    #[test]
    fn canonical_bulk() {
        let items = parse_datagram("PACKTER\n192.168.1.1,10.0.0.1,49305,80,5,This is a test\n10.0.0.1,192.168.1.1,80,49305,0,reply\n");
        let f = flys(&items);
        assert_eq!(f.len(), 2);
        assert_eq!(f[0].flag, 5);
        assert_eq!(f[0].desc, "This is a test");
        assert_eq!(f[0].kind, KIND_LAY);
    }

    #[test]
    fn repeated_header_pairs() {
        let items = parse_datagram("PACKTER\n1.1.1.1,2.2.2.2,1,2,5,a\nPACKTER\n1.1.1.1,2.2.2.2,1,2,5,b\n");
        assert_eq!(flys(&items).len(), 2);
    }

    #[test]
    fn single_line_form() {
        let items = parse_datagram("PACKTER 192.168.1.1,10.0.0.1,49305,80,5,This is a test");
        assert_eq!(flys(&items).len(), 1);
    }

    #[test]
    fn ballistic_and_gateway_kinds() {
        let items = parse_datagram("PACKTERBALLISTIC\n1.1.1.1,2.2.2.2,1,2,3,x\nPACKTERWITHGATEWAY\n1.1.1.1,2.2.2.2,1,2,3,y\n");
        let f = flys(&items);
        assert_eq!(f.len(), 2);
        assert_eq!(f[0].kind, KIND_BALLISTIC);
        assert_eq!(f[1].kind, KIND_GATEWAY);
    }

    #[test]
    fn normalized_coordinates_and_old_header() {
        let items = parse_datagram("PACTER\n0.5,0.5,0.5,0.5,4\n,,0.5,0.5,5\n");
        let f = flys(&items);
        assert_eq!(f.len(), 2);
        assert!((f[0].sx - 0.5).abs() < 1e-6);
        assert!((f[1].sx - 0.5).abs() < 1e-6);
    }

    #[test]
    fn control_msg() {
        let items = parse_datagram("PACKTERMSG\npic01.png,This is a message");
        assert_eq!(items.len(), 1);
        match &items[0] {
            Parsed::Ctrl(Control::Msg { pic, html }) => {
                assert_eq!(pic, "pic01.png");
                assert_eq!(html, "This is a message");
            }
            _ => panic!("expected msg"),
        }
    }

    #[test]
    fn control_sound_and_html_multiline() {
        let items = parse_datagram("PACKTERSOUND\n60,bgm01.wav");
        match &items[0] {
            Parsed::Ctrl(Control::Sound { time, file }) => {
                assert_eq!(time, "60");
                assert_eq!(file, "bgm01.wav");
            }
            _ => panic!("expected sound"),
        }
        let items = parse_datagram("PACKTERHTML\n<html><body>\nmulti\nline</body></html>");
        match &items[0] {
            Parsed::Ctrl(Control::Html { html }) => {
                assert!(html.contains("multi\nline"));
            }
            _ => panic!("expected html"),
        }
    }

    #[test]
    fn frame_roundtrip_layout() {
        let st = Stored {
            ts_ms: 1000,
            ev: FlyEvent { kind: KIND_GATEWAY, sx: 0.25, sy: 0.5, dx: 0.75, dy: 1.0, flag: 7, desc: "abc".into() },
        };
        let frame = encode_fly_frame(&[&st], 1500);
        assert_eq!(frame[0], 2);
        assert_eq!(frame[1], 1);
        assert_eq!(u32::from_le_bytes(frame[4..8].try_into().unwrap()), 1);
        let age = i32::from_le_bytes(frame[8..12].try_into().unwrap());
        assert_eq!(age, 500);
        assert_eq!(frame[28 + 2], KIND_GATEWAY);
        assert_eq!(frame[28 + 3], 3); // descLen
        assert_eq!(&frame[32..35], b"abc");
    }

    #[test]
    fn json_escaping() {
        let c = Control::Voice { text: "say \"hi\"\nplease".into() };
        assert_eq!(control_to_json(&c), r#"{"t":"voice","text":"say \"hi\"\nplease"}"#);
    }
}
