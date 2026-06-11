# インストールとビルド

PACKTER 3.0 は 3 つのコンポーネントからなる。Web ビューアはビルド不要
（静的ファイル＋CDNのThree.js）。

## ブローカー（Rust）

要件: Rust 1.75+ (`cargo`)。

```sh
cd broker
cargo build --release
# 成果物: broker/target/release/packter-broker[.exe]
./target/release/packter-broker ../web
```

主なオプション:

| オプション | 既定 | 説明 |
|---|---|---|
| `<WEB_DIR>` | `../web` | ビューア静的ファイルのディレクトリ |
| `--udp <port>` | 11300 | レガシーUDP受信ポート |
| `--http <port>` | 11380 | HTTP/WebSocket ポート |
| `--boards <N>` | 自動 | ボード枚数（省略時はルールから導出、上限16） |
| `--agent <id>=<board>` | — | エージェントIDをボードに割り当て |
| `--agent-key <id>=<pskfile>` | — | HMAC認証用PSK（ファイルから読む） |
| `--strict` | off | 未割り当てソースをドロップ（`--agent`が許可リストに） |
| `--require-auth` | off | 匿名・未認証を全拒否 |
| `--forward <ip:port>` | — | 受信生データを旧Viewer等へ転送（PACKTERAGENT行は除去） |
| `--record <file>` | — | JSONL録画 |
| `--thmon <conf>` | — | しきい値監視（旧packter.conf互換、`broker/packter.conf.sample`） |
| `--eve <file>` | — | Suricata EVE JSON を tail して取り込み |
| `--eve-board <N>` | 0 | EVE由来イベントのボード |

> Windows + Rust GNU ツールチェーンでビルドする場合、`tokio` を 1.38 系に固定済み
> （新しい tokio は raw-dylib の windows-sys を要求し dlltool が必要になるため）。

## エージェント（C）

要件: C99 コンパイラ ＋ libpcap 開発ヘッダ。glib/OpenSSL は不要（MD5/SHA-256 内蔵）。

```sh
cd agent
make                 # pt_agent pt_sflow pt_netflow pt_thmon pt_replay
make test            # ユニット + ゴールデン（独立Python実装と突き合わせ）
make SANITIZE=1      # ASan/UBSan ビルド
make GEOIP=1         # libGeoIP で -G(PACKTEARTH) を有効化（任意）
```

代表的な使い方:

```sh
pt_agent  -v <broker> -i eth0                 # ライブキャプチャ
pt_agent  -v <broker> -r dump.pcap            # pcap リプレイ
pt_agent  -v <broker> -A core-tap -K psk.txt  # 認証つき（同一ホスト多重・XSS対策）
pt_agent  -v <broker> -B 50 -i eth0           # 50ms バルク送信（帯域節約）
pt_sflow  -v <broker> -l 6343                 # sFlow v4 コレクタ
pt_netflow -v <broker> -l 2055                # NetFlow v9 コレクタ
pt_thmon  -v <broker> -i eth0 -S 0.5          # SYN比率しきい値監視
```

全オプションは各ツール `-h`。

## Webビューア

ビルド不要。ブローカーが配信する。直接配信したい場合は任意の静的サーバで `web/` を
ホストし、`ws://<host>:<port>/ws` に繋がるよう同一オリジンに置く。

- `web/config.json` … サイズ・flag色・ボード名・半径・地形glTF 等（任意・全キー省略可）
- レイアウト別: `http://<broker>:11380/?config=<file>` で代替設定を読込
- 必要外部: Three.js 0.160（jsDelivr CDN）。オフライン運用ではローカルへ同梱に差し替え

## 動作確認

```sh
# ブローカー単体テスト
cd broker && cargo test          # 26 件

# エージェント
cd agent && make test            # ユニット + golden(plain/traceback/bulk/auth)

# 通し（手動）: ブローカー起動 → sender.py → ブラウザ
```
