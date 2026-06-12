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
| `--bind <addr>` | **127.0.0.1** | Listen アドレス（**secure-by-default＝既定loopback**）。リモートのエージェント/ブラウザを受けるには `0.0.0.0` か特定IPを明示指定。UDP・TCP 両方に適用 |
| `--udp <port>` | 11300 | レガシーUDP受信ポート |
| `--http <port>` | 11300 | HTTP/WebSocket ポート（TCP。UDP 11300 とはプロトコルが違うので同番号で共存） |
| `--boards <N>` | 自動 | ボード枚数（省略時はルールから導出、上限16） |
| `--agent <id>=<board>` | — | エージェントIDをボードに割り当て |
| `--agent-key <id>=<pskfile>` | — | HMAC認証用PSK（ファイルから読む） |
| `--strict` | off | 未割り当てソースをドロップ（`--agent`が許可リストに） |
| `--require-auth` | off | 匿名・未認証を全拒否 |
| `--forward <ip:port>` | — | 受信生データを旧Viewer等へ転送（PACKTERAGENT行は除去） |
| `--record <file>` | — | JSONL録画 |
| `--thmon <conf>` | — | 適応型トラフィック監視（CUSUM＋EWMA、旧packter.conf互換、`broker/packter.conf.sample`） |
| `--eve <file>` | — | Suricata EVE JSON を tail して取り込み |
| `--eve-board <N>` | 0 | EVE由来イベントのボード |

> **secure-by-default**: ブローカーは既定で **127.0.0.1（loopback）のみ**を Listen する。
> リモートのエージェントやブラウザから受けるときだけ `--bind 0.0.0.0`（または管理IP）を
> 明示指定する。全インターフェイスへ公開した場合は起動時に警告を出すので、信頼できる
> セグメントで動かし、`--require-auth` で制御コマンド（PACKTERMSG/HTML）をゲートすること。

> Windows + Rust GNU ツールチェーンでビルドする場合、`tokio` を 1.38 系に固定済み
> （新しい tokio は raw-dylib の windows-sys を要求し dlltool が必要になるため）。

## エージェント（C）

要件: C99 コンパイラ ＋ libpcap 開発ヘッダ。glib/OpenSSL は不要（MD5/SHA-256 内蔵）。
ビルドは **autoconf/automake（`configure` 生成時のみ。配布tarballには `configure`
同梱で不要）**。`configure` が libpcap・libm・libmaxminddb とプラットフォーム差を検出
するので、Linux だけでなく **\*BSD / macOS でもそのままビルドできる**（GNU make 専用の
旧 Makefile を廃止）。

> **実行時の libpcap 依存**: ライブキャプチャする `pt_agent` と `pt_thmon` のみ
> libpcap（`libpcap.so` / Linux なら `libpcap0.8`）を必要とする。`pt_sflow` /
> `pt_netflow` / `pt_ipfix` / `pt_replay` は純粋な UDP・ファイル処理で、libpcap を
> リンクしない＝**libpcap 未導入のホストでもそのまま動く**（`configure` がツール毎に
> リンクを出し分ける）。ブローカー（Rust 単一 exe）は libpcap に一切依存しない。

```sh
cd agent
./autogen.sh                    # configure を生成（git clone 時のみ。要 autoconf/automake）
./configure                     # libpcap/libm/libmaxminddb を検出
make                            # pt_agent pt_sflow pt_netflow pt_ipfix pt_thmon pt_replay
make check                      # ユニット + ゴールデン（独立Python実装と突き合わせ）

# 任意のオプション
./configure --with-geoip        # -G(PACKTEARTH) を有効化（libmaxminddb 必要）
./configure --enable-sanitizer  # ASan/UBSan ビルド
./configure --without-geoip     # GeoIP を明示的に無効化
```

代表的な使い方:

```sh
pt_agent  -v <broker> -i eth0                 # ライブキャプチャ
pt_agent  -v <broker> -r dump.pcap            # pcap リプレイ
pt_agent  -v <broker> -A core-tap -K psk.txt  # 認証つき（同一ホスト多重・XSS対策）
pt_agent  -v <broker> -B 50 -i eth0           # 50ms バルク送信（帯域節約）
pt_sflow  -v <broker> -l 6343                 # sFlow v4 コレクタ
pt_netflow -v <broker> -l 2055                # NetFlow v9 コレクタ
pt_ipfix  -v <broker> -l 4739                 # IPFIX(v10) コレクタ
pt_thmon  -v <broker> -i eth0                 # 適応型監視(CUSUM+EWMA、無調整で動作)
```

全オプションは各ツール `-h`。

### sFlow / NetFlow コレクタの運用注意

- `pt_sflow` は **sFlow v4**、`pt_netflow` は **NetFlow v9**、`pt_ipfix` は **IPFIX(v10)**
  のみ解釈する（それ以外のバージョンは無視）。NetFlow v9 と IPFIX はテンプレート/データの
  解析コアを共用（`lib/nf_common.c`）。IPFIX の可変長(0xFFFF)フィールドを含むテンプレートは
  現状デコード対象外（安全に無視）。
- 3 コレクタとも受信ソケットは**デュアルスタック**（`AF_INET6` ＋ `IPV6_V6ONLY=0`）だが、
  **secure-by-default で既定バインドは `127.0.0.1`（loopback）**。実機（ルータ/スイッチ）
  からフローを受けるには `-b <管理IP>`（または `-b 0.0.0.0` / `-b ::`）を明示指定する。
  `-b` は v4／v6 いずれのリテラルも可（v4 指定時は内部で `::ffff:a.b.c.d` に変換、`::` で
  v4+v6 全受信）。フロー*内容*としての IPv6 アドレス（NetFlow/IPFIX の IE 27/28、sFlow の
  サンプルフレーム、pcap の IPv6）は転送方式とは独立に従来から対応済み。
- 公開する場合は**信頼できる管理セグメント**に限定し、`-u <user>` で権限を落とすこと。
  `pt_netflow` のテンプレートキャッシュは上限を持たないため、未知の送信元からの偽テンプレート
  洪水はメモリを消費しうる（管理面に閉じていれば実害なし）。インターネットに晒さないこと。

## Webビューア

ビルド不要。ブローカーが配信する。直接配信したい場合は任意の静的サーバで `web/` を
ホストし、`ws://<host>:<port>/ws` に繋がるよう同一オリジンに置く。

- `web/config.json` … サイズ・flag色・ボード名・半径・地形glTF 等（任意・全キー省略可）
- レイアウト別: `http://<broker>:11300/?config=<file>` で代替設定を読込
- 地球儀ビュー: `?mode=earth`（または `?config=config-earth.json`）。PACKTEARTH
  （`pt_agent -G <MMDB>` か `sender.py --earth`）の緯度経度を地球儀上の大圏アークで描く。
  既定のテクスチャは NASA Blue Marble（パブリックドメイン、CDN 取得）。`config` の
  `earthTexture` で任意の正距円筒画像に差し替え可。オフライン/自己完結にしたい場合は
  `earthStylize:true` で同梱の海岸線アウトラインを着色（海＝青/陸＝緑/砂漠帯＝砂・概略）。`-G` は `./configure --with-geoip`（libmaxminddb）でビルドし、**DB-IP
  「IP to City Lite」MMDB（CC BY 4.0、表示が条件）** を与える。MaxMind GeoLite2
  は再配布不可のため非推奨。`web/assets/compiled/world_ga_worldmap_*.png` は旧
  Packter 由来の素材（CC BY）。
- MMDB の入手: `tools/fetch-geoip.sh [out.mmdb]` で DB-IP Lite を取得できる
  （登録・契約不要の直DL）。**ビルド/CIでは自動取得せず、データも同梱しない** ——
  実行は任意の手動ステップ。手で落とす場合は <https://db-ip.com/db/download/ip-to-city-lite>。
  **CC BY 4.0 のため、データ由来の表示を出す箇所に「IP geolocation by DB-IP」
  （db-ip.com へのリンク）のクレジットが必須**。
- 必要外部: Three.js 0.160（jsDelivr CDN）。オフライン運用ではローカルへ同梱に差し替え

## 動作確認

```sh
# ブローカー単体テスト
cd broker && cargo test          # 27 件

# エージェント
cd agent && ./configure && make check   # ユニット + golden(plain/traceback/bulk/auth)

# 通し（手動）: ブローカー起動 → sender.py → ブラウザ
```
