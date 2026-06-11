//! Threshold-monitor rule engine — the broker-side port of pt_thmon
//! (phase 5 verdict: port). Reads the legacy packter.conf MON_* keys, watches
//! the flag mix of incoming fly events over fixed-count windows, and emits
//! PACKTERMSG / SOUND / VOICE / SKYDOME controls when traffic looks anomalous.
//!
//! Detection (2026-06, replacing the original fixed-ratio thresholds):
//!   * SYN floods — non-parametric CUSUM on the SYN−FIN handshake imbalance,
//!     the classic detector of Wang, Zhang & Shin, "Detecting SYN Flooding
//!     Attacks" (IEEE INFOCOM 2002). A SYN flood leaves SYNs unmatched by
//!     FIN/RST, so the per-window imbalance shifts up; CUSUM accumulates the
//!     standardised shift and alarms at a change point. Self-tuning to the
//!     site's normal balance via an EWMA baseline — no hand-set ratio.
//!   * FIN/RST, ICMP, UDP, PPS — an EWMA control chart (Roberts 1959): alarm
//!     when the window value exceeds mean + k·σ of its exponentially-weighted
//!     baseline. Adaptive band, again no absolute threshold to guess.
//!   * The legacy TH_* keys survive as OPTIONAL absolute hard caps: if set,
//!     a window over the cap alarms regardless of the adaptive state (and even
//!     during warm-up). Unset (the default) => purely adaptive.
//!
//! Tunables (all optional, sane defaults): TH_WARMUP (windows of baseline
//! before adaptive alarms), TH_LAMBDA (EWMA weight), TH_EWMA_K (band σ), and
//! TH_CUSUM_K / TH_CUSUM_H (CUSUM slack and decision interval, in σ units).
//!
//! Deviation from pt_thmon (documented): the broker sees final flags, not TCP
//! header bits, so FIN and RST are one class — the FIN/RST band and the SYN
//! imbalance use the combined FIN/RST count. IPv6 flags (5-9) count in their
//! v4 classes.

use std::collections::HashMap;

use crate::parse::Control;

pub struct ThmonConfig {
    map: HashMap<String, String>,
    // optional absolute hard caps (ratio 0..1, or pps for TH_PPS; <=0 = unused)
    pub th_syn: f32,
    pub th_fin: f32,
    pub th_rst: f32,
    pub th_icmp: f32,
    pub th_udp: f32,
    pub th_pps: f32,
    pub count_max: u64,
    pub interval_ms: u64,
    // adaptive-detector knobs
    pub warmup: u32,
    pub lambda: f32,
    pub ewma_k: f32,
    pub cusum_k: f32,
    pub cusum_h: f32,
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
        let fd = |key: &str, def: f32| map.get(key).and_then(|v| v.parse::<f32>().ok()).unwrap_or(def);
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
            warmup: u("TH_WARMUP", 8) as u32,
            lambda: fd("TH_LAMBDA", 0.3),
            ewma_k: fd("TH_EWMA_K", 3.0),
            cusum_k: fd("TH_CUSUM_K", 0.5),
            cusum_h: fd("TH_CUSUM_H", 5.0),
            map,
        }
    }

    fn get(&self, key: &str) -> Option<&str> {
        self.map.get(key).map(|s| s.as_str()).filter(|s| !s.is_empty())
    }

    /// Whether any legacy absolute hard cap is configured (purely informational
    /// now — adaptive detection runs regardless).
    pub fn any_hard_cap(&self) -> bool {
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

/// Exponentially-weighted moving mean and variance (Roberts/West form).
#[derive(Clone, Copy, Default)]
struct Ewma {
    mean: f32,
    var: f32,
    ready: bool,
}

impl Ewma {
    fn sd(&self) -> f32 {
        self.var.max(0.0).sqrt()
    }
    /// Upper control limit mean + k·σ, with a noise floor so a near-constant
    /// baseline does not make the band hypersensitive.
    fn limit(&self, k: f32, floor: f32) -> f32 {
        self.mean + k * self.sd().max(floor)
    }
    /// Standardised residual (z-score) with the same noise floor.
    fn z(&self, x: f32, floor: f32) -> f32 {
        (x - self.mean) / self.sd().max(floor)
    }
    fn update(&mut self, x: f32, lambda: f32) {
        if !self.ready {
            self.mean = x;
            self.var = 0.0;
            self.ready = true;
            return;
        }
        let dev = x - self.mean;
        self.mean += lambda * dev;
        self.var = (1.0 - lambda) * (self.var + lambda * dev * dev);
    }
}

const RATIO_FLOOR: f32 = 0.02; // 2% — ratio metrics' minimum modelled noise

pub struct Thmon {
    cfg: ThmonConfig,
    counts: Counts,
    window_start_ms: u64,
    windows: u32,
    cooldown_until: u64,
    e_syn: Ewma,
    e_finrst: Ewma,
    e_icmp: Ewma,
    e_udp: Ewma,
    e_pps: Ewma,
    e_imbal: Ewma, // SYN−FIN imbalance baseline, feeds the CUSUM
    cusum_g: f32,
}

#[derive(Default)]
struct AlertBuild {
    msg: String,
    sound: Option<String>,
    voice: String,
    started: bool,
}

impl Thmon {
    pub fn new(cfg: ThmonConfig) -> Thmon {
        Thmon {
            cfg,
            counts: Counts::default(),
            window_start_ms: 0,
            windows: 0,
            cooldown_until: 0,
            e_syn: Ewma::default(),
            e_finrst: Ewma::default(),
            e_icmp: Ewma::default(),
            e_udp: Ewma::default(),
            e_pps: Ewma::default(),
            e_imbal: Ewma::default(),
            cusum_g: 0.0,
        }
    }

    /// Feed one fly-event flag. Closes a window every `count_max` events and
    /// returns alert controls when that window is judged anomalous.
    pub fn on_flag(&mut self, flag: u32, now_ms: u64) -> Vec<Control> {
        if self.counts.all == 0 {
            self.window_start_ms = now_ms;
        }
        self.counts.all += 1;
        match flag % 10 {
            1 | 6 => self.counts.syn += 1,
            2 | 7 => self.counts.finrst += 1,
            3 | 8 => self.counts.udp += 1,
            4 | 9 => self.counts.icmp += 1,
            _ => {}
        }
        if self.counts.all >= self.cfg.count_max {
            let out = self.analyze(now_ms);
            self.counts = Counts::default();
            return out;
        }
        Vec::new()
    }

    fn analyze(&mut self, now_ms: u64) -> Vec<Control> {
        let all = self.counts.all as f32;
        let syn = self.counts.syn as f32 / all;
        let finrst = self.counts.finrst as f32 / all;
        let icmp = self.counts.icmp as f32 / all;
        let udp = self.counts.udp as f32 / all;
        let diff_s = ((now_ms - self.window_start_ms) as f32 / 1000.0).max(0.001);
        let pps = all / diff_s;
        let imbal = syn - finrst;

        self.windows += 1;
        let warming = self.windows <= self.cfg.warmup;

        // Each entry: (class name, observed value, limit it crossed) — values
        // pre-scaled for the message (ratios ×100, pps as-is).
        let mut fired: Vec<(&'static str, f32, f32)> = Vec::new();

        // --- SYN: non-parametric CUSUM on the standardised imbalance ---------
        if self.e_imbal.ready && !warming {
            let z = self.e_imbal.z(imbal, RATIO_FLOOR);
            self.cusum_g = (self.cusum_g + z - self.cfg.cusum_k).max(0.0);
            if self.cusum_g > self.cfg.cusum_h {
                fired.push(("SYN", syn * 100.0, self.e_syn.limit(self.cfg.ewma_k, RATIO_FLOOR) * 100.0));
                self.cusum_g = 0.0; // reset accumulator after raising the alarm
            }
        }
        if self.cfg.th_syn > 0.0 && syn > self.cfg.th_syn && !named(&fired, "SYN") {
            fired.push(("SYN", syn * 100.0, self.cfg.th_syn * 100.0));
        }

        // --- FIN/RST, ICMP, UDP, PPS: EWMA control-chart bands ---------------
        if self.e_finrst.ready && !warming {
            let lim = self.e_finrst.limit(self.cfg.ewma_k, RATIO_FLOOR);
            if finrst > lim {
                fired.push(("FIN", finrst * 100.0, lim * 100.0));
            }
        }
        if self.cfg.th_fin > 0.0 && finrst > self.cfg.th_fin && !named(&fired, "FIN") {
            fired.push(("FIN", finrst * 100.0, self.cfg.th_fin * 100.0));
        }
        if self.cfg.th_rst > 0.0 && finrst > self.cfg.th_rst && !named(&fired, "RST") {
            fired.push(("RST", finrst * 100.0, self.cfg.th_rst * 100.0));
        }

        if self.e_icmp.ready && !warming {
            let lim = self.e_icmp.limit(self.cfg.ewma_k, RATIO_FLOOR);
            if icmp > lim {
                fired.push(("ICMP", icmp * 100.0, lim * 100.0));
            }
        }
        if self.cfg.th_icmp > 0.0 && icmp > self.cfg.th_icmp && !named(&fired, "ICMP") {
            fired.push(("ICMP", icmp * 100.0, self.cfg.th_icmp * 100.0));
        }

        if self.e_udp.ready && !warming {
            let lim = self.e_udp.limit(self.cfg.ewma_k, RATIO_FLOOR);
            if udp > lim {
                fired.push(("UDP", udp * 100.0, lim * 100.0));
            }
        }
        if self.cfg.th_udp > 0.0 && udp > self.cfg.th_udp && !named(&fired, "UDP") {
            fired.push(("UDP", udp * 100.0, self.cfg.th_udp * 100.0));
        }

        if self.e_pps.ready && !warming {
            let lim = self.e_pps.limit(self.cfg.ewma_k, (self.e_pps.mean * 0.25).max(1.0));
            if pps > lim {
                fired.push(("PPS", pps, lim));
            }
        }
        if self.cfg.th_pps > 0.0 && pps > self.cfg.th_pps && !named(&fired, "PPS") {
            fired.push(("PPS", pps, self.cfg.th_pps));
        }

        println!("-------------------------");
        println!("Statistics of {} packet (window {})", self.counts.all, self.windows);
        println!("SYN : {:.4} (mean {:.4})   FIN/RST : {:.4} (mean {:.4})",
                 syn, self.e_syn.mean, finrst, self.e_finrst.mean);
        println!("ICMP: {:.4}   UDP : {:.4}   PPS: {:.2}   CUSUM g: {:.2}/{:.1}",
                 icmp, udp, pps, self.cusum_g, self.cfg.cusum_h);
        if warming {
            println!("(warming up: {}/{} windows)", self.windows, self.cfg.warmup);
        }
        println!("-------------------------");

        // --- adapt baselines, freezing any metric that fired this window -----
        let lambda = self.cfg.lambda;
        if !named(&fired, "SYN") {
            self.e_syn.update(syn, lambda);
            self.e_imbal.update(imbal, lambda);
        }
        if !named(&fired, "FIN") {
            self.e_finrst.update(finrst, lambda);
        }
        if !named(&fired, "ICMP") {
            self.e_icmp.update(icmp, lambda);
        }
        if !named(&fired, "UDP") {
            self.e_udp.update(udp, lambda);
        }
        if !named(&fired, "PPS") {
            self.e_pps.update(pps, lambda);
        }

        if fired.is_empty() || now_ms < self.cooldown_until {
            return Vec::new();
        }
        self.cooldown_until = now_ms + self.cfg.interval_ms;

        let mut b = AlertBuild::default();
        for (name, observed, threshold) in &fired {
            self.add_alert(&mut b, name, *observed, *threshold);
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

fn named(fired: &[(&'static str, f32, f32)], name: &str) -> bool {
    fired.iter().any(|f| f.0 == name)
}

#[cfg(test)]
mod tests {
    use super::*;

    // small windows + short warm-up so tests converge quickly
    const CONF: &str = "
TH_COUNT=100
TH_INTERVAL=0
TH_WARMUP=4
MON_SYN_PIC=pic01.png
MON_SYN_MSG=<b>SYN flood</b>
MON_SYN_SOUND=Detection.wav
MON_SYN_VOICE=SYNが多すぎます
MON_ICMP_MSG=<b>ICMP</b>
MON_OPT_MSG_HEAD=[alert]
MON_SKYDOME_START=red.bmp
";

    /// Feed one full window (count_max events) with the given class mix
    /// (fractions of SYN / FIN-RST / ICMP / UDP; remainder = plain ACK).
    fn window(th: &mut Thmon, syn: f32, fin: f32, icmp: f32, udp: f32, t: u64) -> Vec<Control> {
        let n = th.cfg.count_max;
        let ns = (n as f32 * syn) as u64;
        let nf = (n as f32 * fin) as u64;
        let ni = (n as f32 * icmp) as u64;
        let nu = (n as f32 * udp) as u64;
        let mut out = Vec::new();
        for i in 0..n {
            let flag = if i < ns { 1 }
                else if i < ns + nf { 2 }
                else if i < ns + nf + ni { 4 }
                else if i < ns + nf + ni + nu { 3 }
                else { 0 };
            out = th.on_flag(flag, t + i);
        }
        out
    }

    #[test]
    fn config_parse() {
        let cfg = ThmonConfig::from_str(CONF);
        assert_eq!(cfg.count_max, 100);
        assert_eq!(cfg.warmup, 4);
        assert!((cfg.lambda - 0.3).abs() < 1e-6); // default
        assert!((cfg.cusum_h - 5.0).abs() < 1e-6); // default
        assert_eq!(cfg.get("MON_SYN_PIC"), Some("pic01.png"));
    }

    #[test]
    fn warmup_then_syn_flood_cusum() {
        let mut th = Thmon::new(ThmonConfig::from_str(CONF));
        // baseline: balanced handshakes (syn ~ fin), no alarm during warm-up
        for w in 0..6 {
            let a = window(&mut th, 0.20, 0.18, 0.02, 0.10, 1000 + w * 1000);
            assert!(a.is_empty(), "no alarm on normal baseline (window {w})");
        }
        // SYN flood: SYNs with no matching FIN/RST -> imbalance jumps
        let mut fired = Vec::new();
        for w in 0..4 {
            let a = window(&mut th, 0.90, 0.00, 0.0, 0.05, 7000 + w * 1000);
            if !a.is_empty() { fired = a; break; }
        }
        assert!(!fired.is_empty(), "CUSUM should alarm on SYN flood");
        match &fired[0] {
            Control::Msg { pic, html } => {
                assert_eq!(pic, "pic01.png");
                assert!(html.contains("[alert]"));
                assert!(html.contains("SYN flood"));
            }
            _ => panic!("expected msg first"),
        }
    }

    #[test]
    fn steady_traffic_no_alarm() {
        let mut th = Thmon::new(ThmonConfig::from_str(CONF));
        for w in 0..40 {
            let a = window(&mut th, 0.20, 0.18, 0.05, 0.10, 1000 + w * 1000);
            assert!(a.is_empty(), "steady traffic must not alarm (window {w})");
        }
    }

    #[test]
    fn ewma_band_catches_icmp_spike() {
        let mut th = Thmon::new(ThmonConfig::from_str(CONF));
        for w in 0..8 {
            window(&mut th, 0.20, 0.18, 0.02, 0.10, 1000 + w * 1000); // low ICMP baseline
        }
        // ICMP jumps far above mean + k·σ
        let mut fired = Vec::new();
        for w in 0..3 {
            let a = window(&mut th, 0.10, 0.10, 0.70, 0.05, 9000 + w * 1000);
            if a.iter().any(|c| matches!(c, Control::Msg { html, .. } if html.contains("ICMP"))) {
                fired = a; break;
            }
        }
        assert!(!fired.is_empty(), "EWMA band should catch the ICMP spike");
    }

    #[test]
    fn hard_cap_fires_during_warmup() {
        // an absolute TH_SYN cap alarms immediately, before the baseline exists
        let conf = "TH_COUNT=100\nTH_INTERVAL=0\nTH_SYN=0.5\nMON_SYN_MSG=cap\n";
        let mut th = Thmon::new(ThmonConfig::from_str(conf));
        let a = window(&mut th, 0.80, 0.0, 0.0, 0.0, 1000);
        assert!(a.iter().any(|c| matches!(c, Control::Msg { html, .. } if html.contains("Threshold: 50"))),
                "hard cap TH_SYN=0.5 should fire at 80% even during warm-up");
    }
}
