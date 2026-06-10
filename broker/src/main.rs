use std::net::SocketAddr;
use std::sync::Arc;

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

const UDP_PORT: u16 = 11300;
const HTTP_PORT: u16 = 11380;
const BATCH_MS: u64 = 33;
const EVENT_BYTES: usize = 20;

/// One flying-object event, coordinates already normalized to 0.0..=1.0
/// the same way the legacy XNA viewer normalized them.
#[derive(Clone, Copy, Debug)]
struct FlyEvent {
    sx: f32,
    sy: f32,
    dx: f32,
    dy: f32,
    flag: u32,
}

#[derive(Clone)]
struct AppState {
    frames: broadcast::Sender<Arc<Vec<u8>>>,
}

#[tokio::main]
async fn main() {
    let (frame_tx, _) = broadcast::channel::<Arc<Vec<u8>>>(256);
    let (event_tx, event_rx) = mpsc::channel::<FlyEvent>(65536);

    tokio::spawn(udp_ingest(event_tx));
    tokio::spawn(batcher(event_rx, frame_tx.clone()));

    let state = AppState { frames: frame_tx };
    let web_dir = std::env::args().nth(1).unwrap_or_else(|| "../web".to_string());
    let app = Router::new()
        .route("/ws", get(ws_handler))
        .fallback_service(ServeDir::new(&web_dir))
        .with_state(state);

    let addr = SocketAddr::from(([0, 0, 0, 0], HTTP_PORT));
    println!("packter-broker 3.0.0-alpha.1");
    println!("  legacy UDP ingest : 0.0.0.0:{UDP_PORT}");
    println!("  viewer + websocket: http://localhost:{HTTP_PORT}/  (ws: /ws)");
    println!("  serving web dir   : {web_dir}");
    let listener = tokio::net::TcpListener::bind(addr).await.expect("bind http");
    axum::serve(listener, app).await.expect("serve http");
}

async fn ws_handler(ws: WebSocketUpgrade, State(state): State<AppState>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| ws_client(socket, state))
}

async fn ws_client(mut socket: WebSocket, state: AppState) {
    let mut rx = state.frames.subscribe();
    loop {
        tokio::select! {
            frame = rx.recv() => {
                match frame {
                    Ok(buf) => {
                        if socket.send(Message::Binary(buf.as_ref().clone())).await.is_err() {
                            break;
                        }
                    }
                    // client too slow: skip missed frames and continue from live edge
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

/// Collect events and broadcast one binary frame per BATCH_MS tick.
/// Frame layout (little endian):
///   u8  type     (1 = fly events)
///   u8  reserved
///   u16 reserved
///   u32 count
///   count * { f32 sx, f32 sy, f32 dx, f32 dy, u32 flag }
async fn batcher(mut rx: mpsc::Receiver<FlyEvent>, tx: broadcast::Sender<Arc<Vec<u8>>>) {
    let mut tick = tokio::time::interval(std::time::Duration::from_millis(BATCH_MS));
    let mut pending: Vec<FlyEvent> = Vec::with_capacity(4096);
    loop {
        tokio::select! {
            _ = tick.tick() => {
                if pending.is_empty() || tx.receiver_count() == 0 {
                    pending.clear();
                    continue;
                }
                let mut buf = Vec::with_capacity(8 + pending.len() * EVENT_BYTES);
                buf.push(1u8);
                buf.extend_from_slice(&[0u8; 3]);
                buf.extend_from_slice(&(pending.len() as u32).to_le_bytes());
                for ev in pending.drain(..) {
                    buf.extend_from_slice(&ev.sx.to_le_bytes());
                    buf.extend_from_slice(&ev.sy.to_le_bytes());
                    buf.extend_from_slice(&ev.dx.to_le_bytes());
                    buf.extend_from_slice(&ev.dy.to_le_bytes());
                    buf.extend_from_slice(&ev.flag.to_le_bytes());
                }
                let _ = tx.send(Arc::new(buf));
            }
            ev = rx.recv() => {
                match ev {
                    Some(ev) => pending.push(ev),
                    None => break,
                }
            }
        }
    }
}

async fn udp_ingest(tx: mpsc::Sender<FlyEvent>) {
    let sock = UdpSocket::bind(("0.0.0.0", UDP_PORT)).await.expect("bind udp 11300");
    let mut buf = vec![0u8; 65536];
    loop {
        let Ok((len, _peer)) = sock.recv_from(&mut buf).await else { continue };
        let Ok(text) = std::str::from_utf8(&buf[..len]) else { continue };
        for ev in parse_datagram(text) {
            let _ = tx.try_send(ev);
        }
    }
}

/// Lenient line-oriented parser for the legacy Packter text protocol.
/// Accepted forms in a single datagram:
///   PACKTER\nrec\nrec...          (canonical bulk: one header, many records)
///   PACKTER\nrec\nPACKTER\nrec    (repeated header+record pairs)
///   PACKTER rec                   (header and record on one line)
/// Recognized fly-object headers: PACKTER, PACTER (pre-2.0 typo era),
/// PACKTERBALLISTIC, PACKTERWITHGATEWAY. Non-fly commands (PACKTERMSG,
/// PACKTERSE, ...) switch the parser out of fly mode; their payloads are
/// ignored by this MVP.
fn parse_datagram(text: &str) -> Vec<FlyEvent> {
    const FLY_HEADERS: [&str; 4] = ["PACKTER", "PACTER", "PACKTERBALLISTIC", "PACKTERWITHGATEWAY"];
    let mut events = Vec::new();
    let mut in_fly = false;
    for line in text.lines() {
        let line = line.trim_end_matches('\r');
        if line.is_empty() {
            continue;
        }
        if line.starts_with("PACKTER") || line.starts_with("PACTER") {
            // exact fly header?
            if FLY_HEADERS.contains(&line) {
                in_fly = true;
                continue;
            }
            // "PACKTER rec..." single-line form?
            if let Some((head, rest)) = line.split_once(char::is_whitespace) {
                if FLY_HEADERS.contains(&head) {
                    in_fly = true;
                    if let Some(ev) = parse_record(rest) {
                        events.push(ev);
                    }
                    continue;
                }
            }
            // some other PACKTER* command (MSG/SE/SOUND/...): leave fly mode
            in_fly = false;
            continue;
        }
        if in_fly {
            if let Some(ev) = parse_record(line) {
                events.push(ev);
            }
        }
    }
    events
}

/// record = SRCIP,DSTIP,SRCPORT,DSTPORT,FLAG[,DESCRIPTION]
/// Address/port fields follow the legacy viewer's liberal rules:
/// dotted IPv4, normalized float 0..=1, or integer 1..=65536.
fn parse_record(line: &str) -> Option<FlyEvent> {
    let parts: Vec<&str> = line.splitn(6, ',').collect();
    if parts.len() < 5 {
        return None;
    }
    let sx = addr_to_unit(parts[0])?;
    let dx = addr_to_unit(parts[1])?;
    let sy = port_to_unit(parts[2])?;
    let dy = port_to_unit(parts[3])?;
    let flag: u32 = parts[4].trim().parse().ok()?;
    Some(FlyEvent { sx, sy, dx, dy, flag })
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
        // fold the /64 prefix into 0..1 (stable per-network placement)
        let seg = ip6.segments();
        let hi = ((seg[0] as u64) << 48) | ((seg[1] as u64) << 32) | ((seg[2] as u64) << 16) | seg[3] as u64;
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

    #[test]
    fn canonical_bulk() {
        let evs = parse_datagram("PACKTER\n192.168.1.1,10.0.0.1,49305,80,5,This is a test\n10.0.0.1,192.168.1.1,80,49305,0,reply\n");
        assert_eq!(evs.len(), 2);
        assert_eq!(evs[0].flag, 5);
    }

    #[test]
    fn repeated_header_pairs() {
        let evs = parse_datagram("PACKTER\n192.168.1.1,10.0.0.1,49305,80,5,t\nPACKTER\n192.168.1.1,10.0.0.1,49305,80,5,t\n");
        assert_eq!(evs.len(), 2);
    }

    #[test]
    fn single_line_form() {
        let evs = parse_datagram("PACKTER 192.168.1.1,10.0.0.1,49305,80,5,This is a test");
        assert_eq!(evs.len(), 1);
    }

    #[test]
    fn normalized_coordinates_and_old_header() {
        let evs = parse_datagram("PACTER\n0.5,0.5,0.5,0.5,4\n,,0.5,0.5,5\n");
        assert_eq!(evs.len(), 2);
        assert!((evs[0].sx - 0.5).abs() < 1e-6);
        assert!((evs[1].sx - 0.5).abs() < 1e-6); // empty field defaults to center
    }

    #[test]
    fn non_fly_command_ignored() {
        let evs = parse_datagram("PACKTERMSG\npic01.png,hello\n");
        assert!(evs.is_empty());
    }

    #[test]
    fn five_field_record_without_description() {
        let evs = parse_datagram("PACKTER\n0.0.0.0,255.255.255.255,0,65535,0\n");
        assert_eq!(evs.len(), 1);
    }
}
