# サウンド再生成レシピ（AudioCraft）

裁定（2026-06-10）: 音源はAudioCraftで作り直す。現状 `assets/legacy/se0–9.wav` は
合成プレースホルダ（`tools/gen_se.py`）。本番生成時は以下のプロンプトでAudioGen /
MusicGenを実行し、選定基準と採用テイクをこのファイルに追記すること。

**ライセンス注意**: AudioCraftのコードはMITだが事前学習済み重みはCC-BY-NC 4.0。
生成物をBSDリポジトリへ同梱・配布する前に扱いを確認し、不可なら商用利用可の
代替（Stable Audio Open等）で再生成する。

## SE（AudioGen、各0.3秒以内、モノラル）

| ファイル | flag | プロンプト案 |
|---|---|---|
| se0.wav | TCP ACK | soft short digital blip, neutral, UI confirmation |
| se1.wav | TCP SYN | rising two-tone chirp, connection starting |
| se2.wav | TCP FIN/RST | falling short tone with slight distortion, abrupt cutoff |
| se3.wav | UDP | airy whoosh dart, very short, lightweight |
| se4.wav | ICMP | sonar ping, single, clean |
| se5–9.wav | IPv6側 | 上記と同系統＋金属的な倍音（IPv6識別） |

## 演出音（AudioGen / MusicGen短尺）

| ファイル | 用途 | プロンプト案 |
|---|---|---|
| Detection.wav | 閾値検知 | tense alarm sting, synth brass, 3s |
| Success.wav | トレース成功 | bright resolving chime, major chord, 2s |
| Failed.wav | トレース失敗 | dull descending buzzer, 2s |
| TraceBack.wav | トレース開始 | accelerating radar sweep loop, 4s |
| Packter.wav | 起動音 | heroic short fanfare, chiptune-orchestral hybrid, 4s |

## BGM（MusicGen）

- 平常時: calm ambient electronica, slow pulse, network operations center, loopable 60s
- 攻撃観測時: driving dark synthwave, urgent, loopable 60s

## 選定基準

1ファイルあたり4テイク生成し、(a)立ち上がりの速さ（SEは10ms以内）、
(b)同時多発時の濁りにくさ（高域過多を避ける）、(c)旧SENTIVE音源との
トーン連続性、で選ぶ。
