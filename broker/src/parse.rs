//! Lenient parser for the legacy Packter UDP text protocol.
//! See docs/protocol.md part 1.

pub const KIND_LAY: u8 = 0;
pub const KIND_BALLISTIC: u8 = 1;
pub const KIND_GATEWAY: u8 = 2;
pub const KIND_EARTH: u8 = 3;

/// One flying-object event (coordinates normalized 0..=1, legacy rules).
#[derive(Clone, Debug)]
pub struct FlyEvent {
    pub kind: u8,
    pub sx: f32,
    pub sy: f32,
    pub dx: f32,
    pub dy: f32,
    pub flag: u32,
    pub desc: String,
}

/// Non-fly command forwarded to viewers as a JSON text frame.
#[derive(Clone, Debug)]
pub enum Control {
    Msg { pic: String, html: String },
    Html { html: String },
    Se { file: String },
    Sound { time: String, file: String },
    Voice { text: String },
    Skydome { file: String },
}

pub enum Parsed {
    Fly(FlyEvent),
    Ctrl(Control),
}

/// Fly headers start record mode; control headers consume the rest of
/// the datagram as their payload (2.5 viewer semantics: split('\n', 2)).
pub fn parse_datagram(text: &str) -> Vec<Parsed> {
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
        "PACKTEARTH" => Some(KIND_EARTH),
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

/// PACKTER record = SRCIP,DSTIP,SRCPORT,DSTPORT,FLAG[,DESCRIPTION]
/// PACKTEARTH record = SRCLAT,SRCLON,DSTLAT,DSTLON,FLAG[,DESCRIPTION]
/// (geo strings are "%f,%f", so the comma split yields lat and lon fields)
fn parse_record(line: &str, kind: u8) -> Option<FlyEvent> {
    let parts: Vec<&str> = line.splitn(6, ',').collect();
    if parts.len() < 5 {
        return None;
    }
    if kind == KIND_EARTH {
        let slat: f32 = parts[0].trim().parse().ok()?;
        let slon: f32 = parts[1].trim().parse().ok()?;
        let dlat: f32 = parts[2].trim().parse().ok()?;
        let dlon: f32 = parts[3].trim().parse().ok()?;
        let flag: u32 = parts[4].trim().parse().ok()?;
        let desc = parts.get(5).map(|s| s.to_string()).unwrap_or_default();
        return Some(FlyEvent {
            kind,
            sx: (slon + 180.0) / 360.0,
            sy: (slat + 90.0) / 180.0,
            dx: (dlon + 180.0) / 360.0,
            dy: (dlat + 90.0) / 180.0,
            flag,
            desc,
        });
    }
    let sx = addr_to_unit(parts[0])?;
    let dx = addr_to_unit(parts[1])?;
    let sy = port_to_unit(parts[2])?;
    let dy = port_to_unit(parts[3])?;
    let flag: u32 = parts[4].trim().parse().ok()?;
    let desc = parts.get(5).map(|s| s.to_string()).unwrap_or_default();
    Some(FlyEvent { kind, sx, sy, dx, dy, flag, desc })
}

pub fn addr_to_unit(s: &str) -> Option<f32> {
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

pub fn port_to_unit(s: &str) -> Option<f32> {
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
    fn packtearth_latlon_mapping() {
        // Tokyo (35.68, 139.77) -> Sydney (-33.87, 151.21)
        let items = parse_datagram("PACKTEARTH\n35.680000,139.770000,-33.870000,151.210000,4,geo\n");
        let f = flys(&items);
        assert_eq!(f.len(), 1);
        assert_eq!(f[0].kind, KIND_EARTH);
        assert!((f[0].sx - (139.77 + 180.0) / 360.0).abs() < 1e-4);
        assert!((f[0].sy - (35.68 + 90.0) / 180.0).abs() < 1e-4);
        assert!((f[0].dy - (-33.87 + 90.0) / 180.0).abs() < 1e-4);
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
    fn control_msg_and_sound() {
        let items = parse_datagram("PACKTERMSG\npic01.png,This is a message");
        match &items[0] {
            Parsed::Ctrl(Control::Msg { pic, html }) => {
                assert_eq!(pic, "pic01.png");
                assert_eq!(html, "This is a message");
            }
            _ => panic!("expected msg"),
        }
        let items = parse_datagram("PACKTERSOUND\n60,bgm01.wav");
        match &items[0] {
            Parsed::Ctrl(Control::Sound { time, file }) => {
                assert_eq!(time, "60");
                assert_eq!(file, "bgm01.wav");
            }
            _ => panic!("expected sound"),
        }
    }
}
