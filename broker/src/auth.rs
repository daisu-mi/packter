//! PACKTERAGENT identification + HMAC-SHA256 authentication.
//!
//! Datagram prefix line (added by agents with -A / -K):
//!   PACKTERAGENT <id>                          identification only
//!   PACKTERAGENT <id>,<unix-ts>,<hmac64hex>    authenticated
//! The HMAC covers "<id>,<ts>\n" + the rest of the datagram.
//!
//! Policy:
//!   - if a key is configured for an id, its datagrams MUST verify
//!     (missing/garbage MAC or stale timestamp -> dropped)
//!   - --require-auth drops anonymous and unauthenticated datagrams
//!   - control commands (PACKTERMSG/HTML/SE/...) require an authenticated
//!     agent as soon as any key is configured — this closes the
//!     HTML-injection (XSS) path for anyone who can merely reach UDP 11300

use hmac::{Hmac, Mac};
use sha2::Sha256;
use std::collections::HashMap;

const REPLAY_WINDOW_SECS: i64 = 300;

#[derive(Default)]
pub struct AgentDirectory {
    boards: HashMap<String, u8>,
    keys: HashMap<String, Vec<u8>>,
    pub require_auth: bool,
}

pub struct Peeled<'a> {
    pub rest: &'a str,
    pub agent_id: Option<String>,
    pub board: Option<u8>,
    pub authed: bool,
}

pub enum Verdict<'a> {
    Accept(Peeled<'a>),
    Reject(&'static str),
}

impl AgentDirectory {
    pub fn add_board(&mut self, id: &str, board: u8) {
        self.boards.insert(id.to_string(), board);
    }

    pub fn add_key(&mut self, id: &str, key: &[u8]) {
        self.keys.insert(id.to_string(), key.to_vec());
    }

    pub fn has_keys(&self) -> bool {
        !self.keys.is_empty()
    }

    /// Controls are open only when no auth is configured at all.
    pub fn controls_need_auth(&self) -> bool {
        self.has_keys() || self.require_auth
    }

    pub fn peel<'a>(&self, text: &'a str, now_unix: i64) -> Verdict<'a> {
        const HDR: &str = "PACKTERAGENT ";
        if !text.starts_with(HDR) {
            if self.require_auth {
                return Verdict::Reject("anonymous datagram (require-auth)");
            }
            return Verdict::Accept(Peeled { rest: text, agent_id: None, board: None, authed: false });
        }
        let (line, rest) = match text.split_once('\n') {
            Some((l, r)) => (l.trim_end_matches('\r'), r),
            None => (text.trim_end_matches('\r'), ""),
        };
        let fields: Vec<&str> = line[HDR.len()..].split(',').collect();
        let id = fields[0].trim();
        if id.is_empty() {
            return Verdict::Reject("empty agent id");
        }
        let board = self.boards.get(id).copied();

        if let Some(key) = self.keys.get(id) {
            if fields.len() != 3 {
                return Verdict::Reject("agent has key but datagram is unsigned");
            }
            let ts: i64 = match fields[1].trim().parse() {
                Ok(v) => v,
                Err(_) => return Verdict::Reject("bad timestamp"),
            };
            if (now_unix - ts).abs() > REPLAY_WINDOW_SECS {
                return Verdict::Reject("timestamp outside replay window");
            }
            let mut mac = Hmac::<Sha256>::new_from_slice(key).expect("hmac key");
            mac.update(format!("{id},{ts}\n").as_bytes());
            mac.update(rest.as_bytes());
            let want = mac.finalize().into_bytes();
            let got = match hex_decode32(fields[2].trim()) {
                Some(b) => b,
                None => return Verdict::Reject("bad hmac encoding"),
            };
            if !ct_eq(&want, &got) {
                return Verdict::Reject("hmac mismatch");
            }
            return Verdict::Accept(Peeled { rest, agent_id: Some(id.to_string()), board, authed: true });
        }

        if self.require_auth {
            return Verdict::Reject("unknown agent id (require-auth)");
        }
        Verdict::Accept(Peeled { rest, agent_id: Some(id.to_string()), board, authed: false })
    }
}

fn hex_decode32(s: &str) -> Option<[u8; 32]> {
    if s.len() != 64 {
        return None;
    }
    let mut out = [0u8; 32];
    for i in 0..32 {
        out[i] = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).ok()?;
    }
    Some(out)
}

fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sign(id: &str, ts: i64, key: &[u8], payload: &str) -> String {
        let mut mac = Hmac::<Sha256>::new_from_slice(key).unwrap();
        mac.update(format!("{id},{ts}\n").as_bytes());
        mac.update(payload.as_bytes());
        let hex: String = mac.finalize().into_bytes().iter()
            .map(|b| format!("{b:02x}")).collect();
        format!("PACKTERAGENT {id},{ts},{hex}\n{payload}")
    }

    fn dir() -> AgentDirectory {
        let mut d = AgentDirectory::default();
        d.add_board("pcap1", 0);
        d.add_board("sflow1", 2);
        d.add_key("sflow1", b"secret-psk");
        d
    }

    const PAYLOAD: &str = "PACKTER\n1.1.1.1,2.2.2.2,1,2,0,x\n";

    #[test]
    fn anonymous_passes_by_default() {
        let d = dir();
        match d.peel(PAYLOAD, 1000) {
            Verdict::Accept(p) => {
                assert!(p.agent_id.is_none());
                assert_eq!(p.rest, PAYLOAD);
            }
            _ => panic!(),
        }
    }

    #[test]
    fn ident_only_agent_maps_board() {
        let d = dir();
        let text = format!("PACKTERAGENT pcap1\n{PAYLOAD}");
        match d.peel(&text, 1000) {
            Verdict::Accept(p) => {
                assert_eq!(p.agent_id.as_deref(), Some("pcap1"));
                assert_eq!(p.board, Some(0));
                assert!(!p.authed);
                assert_eq!(p.rest, PAYLOAD);
            }
            _ => panic!(),
        }
    }

    #[test]
    fn signed_agent_verifies_and_strips() {
        let d = dir();
        let text = sign("sflow1", 5000, b"secret-psk", PAYLOAD);
        match d.peel(&text, 5100) {
            Verdict::Accept(p) => {
                assert!(p.authed);
                assert_eq!(p.board, Some(2));
                assert_eq!(p.rest, PAYLOAD);
            }
            _ => panic!("should verify"),
        }
    }

    #[test]
    fn tampered_payload_rejected() {
        let d = dir();
        let text = sign("sflow1", 5000, b"secret-psk", PAYLOAD)
            .replace("1.1.1.1", "6.6.6.6");
        assert!(matches!(d.peel(&text, 5100), Verdict::Reject(_)));
    }

    #[test]
    fn stale_timestamp_rejected() {
        let d = dir();
        let text = sign("sflow1", 5000, b"secret-psk", PAYLOAD);
        assert!(matches!(d.peel(&text, 5000 + 301), Verdict::Reject(_)));
    }

    #[test]
    fn keyed_agent_must_sign() {
        let d = dir();
        let text = format!("PACKTERAGENT sflow1\n{PAYLOAD}");
        assert!(matches!(d.peel(&text, 1000), Verdict::Reject(_)));
    }

    #[test]
    fn wrong_key_rejected() {
        let d = dir();
        let text = sign("sflow1", 5000, b"WRONG", PAYLOAD);
        assert!(matches!(d.peel(&text, 5100), Verdict::Reject(_)));
    }

    #[test]
    fn require_auth_drops_anonymous() {
        let mut d = dir();
        d.require_auth = true;
        assert!(matches!(d.peel(PAYLOAD, 1000), Verdict::Reject(_)));
        let ident = format!("PACKTERAGENT pcap1\n{PAYLOAD}");
        assert!(matches!(d.peel(&ident, 1000), Verdict::Reject(_)));
        let signed = sign("sflow1", 5000, b"secret-psk", PAYLOAD);
        assert!(matches!(d.peel(&signed, 5100), Verdict::Accept(_)));
    }

    #[test]
    fn controls_gating_flag() {
        let mut open = AgentDirectory::default();
        assert!(!open.controls_need_auth());
        open.require_auth = true;
        assert!(open.controls_need_auth());
        assert!(dir().controls_need_auth());
    }
}
