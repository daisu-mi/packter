# Changelog

セマンティックバージョニング。日付は 2026 年。

## [3.0.0-beta.1] - 2026-06-11

Packter 2.x（Agent 2.5 / Viewer 2.4、XNA）からの全面現代化。3コンポーネント構成
（Rustブローカー＋Cエージェント＋Webビューア）。

### Added
- **ブローカー（新規・Rust単一バイナリ）**: 旧UDP 11300互換受信、33msバッチの
  WebSocketバイナリ配信（ワイヤv3）、Webビューア静的配信
- **巻き戻し再生**: 直近5分のローリングバッファ（ビューア／ブローカー両側）、
  途中参加クライアントへのバックフィル、時間スクラブ・コマ送り
- **N枚ボード配置**: 地面に壁を円状配置（真上から見ると三角〜六角“状”）。
  `--boards N` ＋ `--agent <id>=<board>`。Receiver は宛先ボード、Agent群が周囲
- **ボードキャプション**: エージェントの `-A <id>` がライブでボード名に反映
- **AgentID＋PSK認証**: `pt_agent -A <id> -K <pskfile>`（HMAC-SHA256、±300sリプレイ窓）。
  鍵設定時は制御コマンド（MSG/HTML等）を認証済みのみ受理＝UDP到達によるHTML注入(XSS)封鎖
- **エージェント選別**: ブローカー `--strict`（許可リスト方式）、ビューア数字キーで
  ボードのライブ非表示
- **しきい値監視**: ブローカー `--thmon`（旧 packter.conf の MON_* 互換、しきい値は TH_*）
- **Suricata EVE 取り込み**: `--eve eve.json`（Snort unsock 連携の現代化）
- **PACKTEARTH** 受理（GeoIP緯度経度→座標）
- **録画**: JSONL（`--record`）、レガシー転送（`--forward`）
- **ビューア**: flag色のball.x実メッシュ（陰影）、クリック選択＋DESCRIPTION、
  PACKTERMSG/HTMLトースト（sandbox iframe）、WebAudio、Web Speech、スカイドーム差替、
  PNG保存（Pキー）、`config.json`、コンパイル済み2.4アセット採用

### Changed
- **エージェント**: PackterAgent 2.5 を C で全面リライト。依存を libpcap のみに
  （glib→自前KVマップ、OpenSSL→内蔵MD5）。`packter_ctx` 構造体化、プロトコル処理を
  テーブル駆動に統合、autotools→Makefile、IPv6常時対応（ビルドフラグ不要）
- **バルク送信** `-B <ms>`: 複数レコードを1データグラム化（Viewer 2.4互換）
- 設定は Config.txt → config.json、XACT音響→WebAudio、softalk→Web Speech

### Fixed（2.5由来のバグ）
- IPv6レコードが一度も送出されていなかった（拡張ヘッダ処理の誤り）
- ループバック(DLT_NULL)キャプチャが常に無処理だった
- `sprintf` 自己重複の未定義動作、スナップ長超過のOOB read、各種リーク
- NetFlow TCPフラグのオフセット判定、thmon のサウンドキー誤接続

### Deprecated / Removed
- `packter_tc.pl`（InterTrackトレースバック）廃止 → プラグインAPI仕様のみ温存
- VR（2017 Vive版）一時無効化、Gateway軌道の描画一時無効化

### Known issues / Roadmap
- SE/BGM音源は合成プレースホルダ。AudioCraft で再生成予定（`web/assets/sound/RECIPES.md`）
- エージェント認証HMACの内部連結バッファが2048バイトで切り詰め（現状の最大ペイロード
  ~1.3KBでは未発現。バルク上限を上げる前にストリーミング化が必要）
- バックフィルがリングのロックを保持したままエンコード（高負荷時の最適化余地）
- sFlow v5 / IPFIX のブローカー直収（実機データ待ち）
