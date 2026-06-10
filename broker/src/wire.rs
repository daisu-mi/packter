//! Broker -> viewer wire encoding. See docs/protocol.md part 2.

use crate::parse::{Control, FlyEvent};

pub struct Stored {
    pub ts_ms: u64,
    pub ev: FlyEvent,
}

/// Binary fly frame v2 (little endian):
///   u8 ver=2, u8 type=1, u16 reserved, u32 count, then per event:
///   i32 ageMs, f32 sx, f32 sy, f32 dx, f32 dy, u16 flag, u8 kind,
///   u8 descLen, descLen bytes UTF-8
pub fn encode_fly_frame(events: &[&Stored], now_ms: u64) -> Vec<u8> {
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

pub fn json_escape(s: &str) -> String {
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

pub fn control_to_json(c: &Control) -> String {
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parse::KIND_GATEWAY;

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
        assert_eq!(frame[30], KIND_GATEWAY);
        assert_eq!(frame[31], 3);
        assert_eq!(&frame[32..35], b"abc");
    }

    #[test]
    fn json_escaping() {
        let c = Control::Voice { text: "say \"hi\"\nplease".into() };
        assert_eq!(control_to_json(&c), r#"{"t":"voice","text":"say \"hi\"\nplease"}"#);
    }
}
