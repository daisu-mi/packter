//! Threshold-monitor rule engine — the broker-side port of pt_thmon
//! (phase 5 verdict: port). Reads the legacy packter.conf MON_* keys plus
//! TH_* threshold keys, watches the flag mix of incoming fly events, and
//! emits PACKTERMSG / SOUND / VOICE / SKYDOME controls when a window
//! exceeds a threshold.
//!
//! Deviation from pt_thmon (documented): the broker sees final flags, not
//! TCP header bits, so FIN and RST are one class — TH_FIN and TH_RST both
//! compare against the combined FIN/RST ratio. IPv6 flags (5-9) count in
//! their v4 classes.

use std::collections::HashMap;

use crate::parse::Control;

pub struct ThmonConfig {
    map: HashMap<String, String>,
    pub th_syn: f32,
    pub th_fin: f32,
    pub th_rst: f32,
    pub th_icmp: f32,
    pub th_udp: f32,
    pub th_pps: f32,
    pub count_max: u64,
    pub interval_ms: u64,
}

impl ThmonConfig {
    pub fn from_file(path: &str) -> std::io::Result<ThmonConfig> {
        let text = std::fs::read_to_string(path)?;
        Ok(Self::from_str(&text))
    }

    pub fn from_str(text: &str) -> ThmonConfig {
        let mut map = HashMap::new();
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some((k, v)) = line.split_once('=') {
                let k = k.trim().to_string();
                let v = v.trim().to_string();
                if !k.is_empty() && !v.is_empty() {
                    map.entry(k).or_insert(v); // first occurrence wins (2.5)
                }
            }
        }
        let f = |key: &str| map.get(key).and_then(|v| v.parse::<f32>().ok()).unwrap_or(-1.0);
        let u = |key: &str, def: u64| map.get(key).and_then(|v| v.parse::<u64>().ok()).unwrap_or(def);
        ThmonConfig {
            th_syn: f("TH_SYN"),
            th_fin: f("TH_FIN"),
            th_rst: f("TH_RST"),
            th_icmp: f("TH_ICMP"),
            th_udp: f("TH_UDP"),
            th_pps: f("TH_PPS"),
            count_max: u("TH_COUNT", 500),
            interval_ms: u("TH_INTERVAL", 30) * 1000,
            map,
        }
    }

    fn get(&self, key: &str) -> Option<&str> {
        self.map.get(key).map(|s| s.as_str()).filter(|s| !s.is_empty())
    }

    pub fn any_threshold(&self) -> bool {
        [self.th_syn, self.th_fin, self.th_rst, self.th_icmp, self.th_udp, self.th_pps]
            .iter().any(|t| *t > 0.0)
    }
}

#[derive(Default)]
struct Counts {
    all: u64,
    syn: u64,
    finrst: u64,
    icmp: u64,
    udp: u64,
}

pub struct Thmon {
    cfg: ThmonConfig,
    counts: Counts,
    window_start_ms: u64,
    stop_ms: u64,
}

struct AlertBuild {
    msg: String,
    sound: Option<String>,
    voice: String,
    started: bool,
}

impl Thmon {
    pub fn new(cfg: ThmonConfig) -> Thmon {
        Thmon { cfg, counts: Counts::default(), window_start_ms: 0, stop_ms: 0 }
    }

    /// Feed one fly-event flag. Returns alert controls when a window closed
    /// with exceeded thresholds (legacy: count windows separated by
    /// `interval` of quiet after each analysis).
    pub fn on_flag(&mut self, flag: u32, now_ms: u64) -> Vec<Control> {
        if self.counts.all == 0 {
            self.window_start_ms = now_ms;
        }
        if now_ms.saturating_sub(self.stop_ms) > self.cfg.interval_ms {
            self.counts.all += 1;
            match flag % 10 {
                1 | 6 => self.counts.syn += 1,
                2 | 7 => self.counts.finrst += 1,
                3 | 8 => self.counts.udp += 1,
                4 | 9 => self.counts.icmp += 1,
                _ => {}
            }
        }
        if self.counts.all >= self.cfg.count_max {
            let out = self.analyze(now_ms);
            self.stop_ms = now_ms;
            self.counts = Counts::default();
            return out;
        }
        Vec::new()
    }

    fn analyze(&self, now_ms: u64) -> Vec<Control> {
        let all = self.counts.all as f32;
        let mon_syn = self.counts.syn as f32 / all;
        let mon_finrst = self.counts.finrst as f32 / all;
        let mon_icmp = self.counts.icmp as f32 / all;
        let mon_udp = self.counts.udp as f32 / all;
        let diff_s = ((now_ms - self.window_start_ms) as f32 / 1000.0).max(1.0);
        let mon_pps = all / diff_s;

        println!("-------------------------");
        println!("Statistics of {} packet", self.counts.all);
        println!("SYN : {:.4}   FIN/RST : {:.4}", mon_syn, mon_finrst);
        println!("ICMP: {:.4}   UDP : {:.4}   PPS: {:.4}", mon_icmp, mon_udp, mon_pps);
        println!("-------------------------");

        let mut b = AlertBuild {
            msg: String::new(),
            sound: None,
            voice: String::new(),
            started: false,
        };

        let checks: [(f32, f32, &str, f32); 6] = [
            (mon_syn, self.cfg.th_syn, "SYN", 100.0),
            (mon_finrst, self.cfg.th_fin, "FIN", 100.0),
            (mon_finrst, self.cfg.th_rst, "RST", 100.0),
            (mon_icmp, self.cfg.th_icmp, "ICMP", 100.0),
            (mon_udp, self.cfg.th_udp, "UDP", 100.0),
            (mon_pps, self.cfg.th_pps, "PPS", 1.0),
        ];
        for (observed, threshold, name, scale) in checks {
            if threshold > 0.0 && observed > threshold {
                self.add_alert(&mut b, name, observed * scale, threshold * scale);
            }
        }
        if !b.started {
            return Vec::new();
        }

        let mut msg = b.msg;
        if let Some(foot) = self.cfg.get("MON_OPT_MSG_FOOT") {
            msg.push_str(foot);
        }
        let mut out = vec![Control::Msg {
            pic: msg.split(',').next().unwrap_or("").to_string(),
            html: msg.split_once(',').map(|(_, h)| h.to_string()).unwrap_or_default(),
        }];
        if let Some(file) = b.sound {
            out.push(Control::Sound { time: "0.1".into(), file });
        }
        if !b.voice.is_empty() {
            out.push(Control::Voice { text: b.voice });
        }
        if let Some(sky) = self.cfg.get("MON_SKYDOME_START") {
            out.push(Control::Skydome { file: sky.to_string() });
        }
        out
    }

    fn add_alert(&self, b: &mut AlertBuild, name: &str, observed: f32, threshold: f32) {
        if !b.started {
            // first exceeded metric provides PIC, then the shared header
            if let Some(pic) = self.cfg.get(&format!("MON_{name}_PIC")) {
                b.msg.push_str(pic);
            }
            b.msg.push(',');
            if let Some(head) = self.cfg.get("MON_OPT_MSG_HEAD") {
                b.msg.push_str(head);
            }
            if let Some(vh) = self.cfg.get("MON_OPT_VOICE_HEAD") {
                b.voice.push_str(vh);
            }
            b.started = true;
        }
        if let Some(m) = self.cfg.get(&format!("MON_{name}_MSG")) {
            b.msg.push_str(m);
        }
        b.msg.push_str(self.cfg.get("MONITOR").unwrap_or(" Observed:"));
        b.msg.push_str(&format!(" {:.0}", observed));
        b.msg.push_str(self.cfg.get("THRESHOLD").unwrap_or(" Threshold:"));
        b.msg.push_str(&format!(" {:.0}", threshold));
        if b.sound.is_none() {
            if let Some(s) = self.cfg.get(&format!("MON_{name}_SOUND")) {
                b.sound = Some(s.to_string());
            }
        }
        if let Some(v) = self.cfg.get(&format!("MON_{name}_VOICE")) {
            b.voice.push_str(v);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const CONF: &str = "
# test conf
TH_SYN=0.5
TH_COUNT=10
TH_INTERVAL=0
MON_SYN_PIC=pic01.png
MON_SYN_MSG=<b>SYN flood</b>
MON_SYN_SOUND=Detection.wav
MON_SYN_VOICE=SYNが多すぎます
MON_OPT_MSG_HEAD=[alert]
MON_SKYDOME_START=red.bmp
";

    #[test]
    fn config_parse() {
        let cfg = ThmonConfig::from_str(CONF);
        assert!((cfg.th_syn - 0.5).abs() < 1e-6);
        assert_eq!(cfg.count_max, 10);
        assert!(cfg.any_threshold());
        assert_eq!(cfg.get("MON_SYN_PIC"), Some("pic01.png"));
    }

    #[test]
    fn syn_flood_alerts() {
        let mut th = Thmon::new(ThmonConfig::from_str(CONF));
        let mut alerts = Vec::new();
        for i in 0..10 {
            // 8 of 10 are SYN -> ratio 0.8 > 0.5
            let flag = if i < 8 { 1 } else { 0 };
            alerts = th.on_flag(flag, 1000 + i);
        }
        assert_eq!(alerts.len(), 4); // msg + sound + voice + skydome
        match &alerts[0] {
            Control::Msg { pic, html } => {
                assert_eq!(pic, "pic01.png");
                assert!(html.contains("[alert]"));
                assert!(html.contains("SYN flood"));
                assert!(html.contains("Observed: 80"));
                assert!(html.contains("Threshold: 50"));
            }
            _ => panic!("expected msg first"),
        }
        match &alerts[1] {
            Control::Sound { file, .. } => assert_eq!(file, "Detection.wav"),
            _ => panic!("expected sound"),
        }
    }

    #[test]
    fn quiet_traffic_no_alert() {
        let mut th = Thmon::new(ThmonConfig::from_str(CONF));
        for i in 0..30 {
            let alerts = th.on_flag(0, 1000 + i); // all ACK
            assert!(alerts.is_empty());
        }
    }

    #[test]
    fn interval_gates_counting() {
        let conf = "TH_SYN=0.5\nTH_COUNT=5\nTH_INTERVAL=10\nMON_SYN_MSG=x\n";
        let mut th = Thmon::new(ThmonConfig::from_str(conf));
        // first window fires (stop_ms=0, now > interval)
        let mut fired = false;
        for i in 0..5 {
            if !th.on_flag(1, 20000 + i).is_empty() {
                fired = true;
            }
        }
        assert!(fired);
        // immediately after, counting is gated for interval_ms
        for i in 0..20 {
            assert!(th.on_flag(1, 20010 + i).is_empty());
        }
        // after the quiet interval, a new window can fire again
        let mut fired2 = false;
        for i in 0..5 {
            if !th.on_flag(1, 40000 + i).is_empty() {
                fired2 = true;
            }
        }
        assert!(fired2);
    }
}
