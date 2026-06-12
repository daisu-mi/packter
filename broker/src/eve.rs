//! Suricata EVE JSON ingest — the modern replacement for the Snort
//! "-A unsock" integration (phase 5 verdict: 更新). Tails an eve.json
//! file and turns alert records into fly events with an
//! "Incident:<signature>" description, Snort-bridge style.

use crate::parse::{addr_to_unit, port_to_unit, FlyEvent, KIND_LAY};

pub fn parse_eve_line(line: &str) -> Option<FlyEvent> {
    let v: serde_json::Value = serde_json::from_str(line).ok()?;
    if v["event_type"].as_str()? != "alert" {
        return None;
    }
    let src_ip = v["src_ip"].as_str()?;
    let dest_ip = v["dest_ip"].as_str()?;
    let v6 = src_ip.contains(':');
    let proto = v["proto"].as_str().unwrap_or("");
    let flag: u32 = match proto.to_ascii_uppercase().as_str() {
        "TCP" => if v6 { 5 } else { 0 },
        "UDP" => if v6 { 8 } else { 3 },
        "ICMP" | "IPV6-ICMP" | "ICMPV6" => if v6 { 9 } else { 4 },
        _ => 0,
    };
    let sx = addr_to_unit(src_ip)?;
    let dx = addr_to_unit(dest_ip)?;
    let sy = v["src_port"].as_u64()
        .and_then(|p| port_to_unit(&p.to_string())).unwrap_or(0.5);
    let dy = v["dest_port"].as_u64()
        .and_then(|p| port_to_unit(&p.to_string())).unwrap_or(0.5);
    let sig = v["alert"]["signature"].as_str().unwrap_or("unknown");
    let sid = v["alert"]["signature_id"].as_u64().unwrap_or(0);
    Some(FlyEvent {
        kind: KIND_LAY,
        sx,
        sy,
        dx,
        dy,
        flag,
        src_board: 1,   // overwritten by --eve-board; default sender
        dst_board: 0,   // receiver
        desc: format!("Incident:{sig} sid:{sid}"),
    })
}

/// Tail `path` (starting at EOF) and forward alert fly events.
pub async fn tail_eve(path: String, board: u8, tx: tokio::sync::mpsc::Sender<crate::parse::Parsed>) {
    use tokio::io::{AsyncReadExt, AsyncSeekExt};

    let mut file = match tokio::fs::File::open(&path).await {
        Ok(f) => f,
        Err(e) => {
            eprintln!("eve: cannot open {path}: {e}");
            return;
        }
    };
    let mut pos = file.seek(std::io::SeekFrom::End(0)).await.unwrap_or(0);
    let mut carry = String::new();
    let mut tick = tokio::time::interval(std::time::Duration::from_millis(300));
    loop {
        tick.tick().await;
        let len = match tokio::fs::metadata(&path).await {
            Ok(m) => m.len(),
            Err(_) => continue,
        };
        if len < pos {
            pos = 0; // rotated/truncated: start over
            let _ = file.seek(std::io::SeekFrom::Start(0)).await;
        }
        if len == pos {
            continue;
        }
        let mut chunk = String::new();
        if file.read_to_string(&mut chunk).await.is_err() {
            continue;
        }
        pos = len;
        carry.push_str(&chunk);
        while let Some(nl) = carry.find('\n') {
            let line: String = carry.drain(..=nl).collect();
            if let Some(mut ev) = parse_eve_line(line.trim_end()) {
                ev.src_board = board;
                let _ = tx.try_send(crate::parse::Parsed::Fly(ev));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alert_line() {
        let line = r#"{"timestamp":"2026-06-10T12:00:00","event_type":"alert","src_ip":"192.168.1.50","src_port":49152,"dest_ip":"10.0.0.80","dest_port":80,"proto":"TCP","alert":{"action":"allowed","signature_id":2019401,"signature":"ET SCAN NMAP -sS window 1024","severity":2}}"#;
        let ev = parse_eve_line(line).expect("parse");
        assert_eq!(ev.flag, 0);
        assert!(ev.desc.contains("ET SCAN NMAP"));
        assert!(ev.desc.contains("sid:2019401"));
        assert!((ev.sy - 49152.0 / 65536.0).abs() < 1e-4);
    }

    #[test]
    fn non_alert_ignored() {
        let line = r#"{"event_type":"flow","src_ip":"1.1.1.1","dest_ip":"2.2.2.2","proto":"TCP"}"#;
        assert!(parse_eve_line(line).is_none());
    }

    #[test]
    fn icmp_v6_alert() {
        let line = r#"{"event_type":"alert","src_ip":"2001:db8::1","dest_ip":"2001:db8::2","proto":"IPv6-ICMP","alert":{"signature":"x","signature_id":1}}"#;
        let ev = parse_eve_line(line).expect("parse");
        assert_eq!(ev.flag, 9);
        assert!((ev.sy - 0.5).abs() < 1e-6); // no ports -> center
    }
}
