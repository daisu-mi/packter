# Packter プロトコル仕様

## 第1部: レガシーUDPプロトコル v1.1（互換仕様の正典化）

実装から逆引きで文書化（Wiki 2010年版＋Viewer 2.4 / Agent 2.5 / 3.0 ソース）。
ブローカーの互換受信（UDP 11300）はこの仕様を受理する。

### データグラム構造

行指向テキスト。**寛容受信の裁定（2026-06）**: 以下すべてを受理する。

1. 正規形バルク: `PACKTER\n` ＋ レコード行の列挙（推奨。Agent 3.0 `-B` / Viewer 2.4も受理）
2. 組の繰り返し: `PACKTER\nレコード\nPACKTER\nレコード...`（注: Viewer 2.4は2件目以降を捨てる）
3. 1行形式: `PACKTER レコード`（注: Viewer 2.4は無視する）
4. 単発: `PACKTER\nレコード`（Agent 2.5の送信形）

### 飛翔体コマンド

| ヘッダ | 軌道 | 備考 |
|---|---|---|
| `PACKTER` | 直線（Lay） | 標準 |
| `PACTER` | 直線 | 1.x時代のtypoヘッダ。互換受理 |
| `PACKTERBALLISTIC` | 弾道（放物線） | Wiki未記載 |
| `PACKTERWITHGATEWAY` | sender→gateway→receiver折れ線 | Wiki未記載 |

レコード: `SRCIP,DSTIP,SRCPORT,DSTPORT,FLAG[,DESCRIPTION]`

- アドレス欄: IPv4ドット表記 / IPv6コロン表記 / 正規化座標0–1の小数 / 整数1–65536（/65536で正規化）/ 空欄（中央=0.5）
- ポート欄: 0–1小数 / 整数1–65536 / 空欄
- FLAG: 0=TCP ACK, 1=SYN, 2=FIN/RST, 3=UDP, 4=ICMP, 5–9=IPv6側の同順。
  flagbase(`-f`)が加算され、ビューアは flag%10 で色/モデル選択
- ICMPの場合 SRCPORT=type×256, DSTPORT=code×256
- DESCRIPTION: 表示文字列。トレースバック時は `MD5ハッシュ32hex-トレースバックサーバIP`
- トレースバックMD5: IPv4=可変フィールド(tos/len/off/ttl/sum＋オプション域)ゼロ化したヘッダ＋ペイロード先頭8バイト。
  IPv6=先頭バイトの上位ニブルマスク＋hop limitゼロ化、先頭48バイト（2.5の癖を仕様化）

### 制御コマンド（ヘッダ行の後、データグラム残り全部がペイロード）

| ヘッダ | ペイロード | 意味 |
|---|---|---|
| `PACKTERMSG` | `PICFILE,HTML` | キャラ画像つきメッセージ |
| `PACKTERHTML` | HTML | HTML表示 |
| `PACKTERSE` | ファイル名 | 効果音（同時再生上限あり） |
| `PACKTERSOUND` | `秒数,ファイル名` | BGM。>0=指定秒 / <0=ループ / 0=停止 |
| `PACKTERVOICE` | テキスト | 音声合成（`/X:opt` 形式のsoftalkオプションは除去対象） |
| `PACKTERSKYDOMETEXTURE` | ファイル名 | スカイドーム差し替え |
| `PACKTEARTH` | レコード（座標=緯度,経度） | GeoIPモードの飛翔体（ポート欄なし: `SRCGEO,DSTGEO,FLAG,DESC`） |

## 第2部: ブローカー⇔Webビューア ワイヤ v2

### バイナリ（飛翔体、WSバイナリフレーム、LE）

```
u8 ver=3, u8 type=1, u16 reserved, u32 count
count × {
  i32 ageMs        // 発射からの経過（ライブ≒0、バックフィルで過去分を再現）
  f32 sx, sy, dx, dy   // 正規化座標
  u16 flag
  u8  kind         // 0=lay, 1=ballistic, 2=gateway, 3=earth
  u8  srcBoard     // N面配置: 出発ボード index（既定0）
  u8  dstBoard     // 到着ボード index（既定1）
  u8  descLen
  descLen bytes    // UTF-8 DESCRIPTION
}
```

### N面配置（ボード振り分け）

ブローカーがデータグラムの**送信元アドレス**でボードを割り当てる（Agent無改修）:
```
packter-broker --board 192.168.1.5=2 --board 10.0.0.0/8=3 [--eve-board 4]
```
ルール形式は「IP=index」（v4/v6完全一致）または「CIDR=index」（v4）。
不一致は board 0。ビューア側は config の `boards[]` 配列（position /
rotationY / size / texture / name）で配置を定義する。レイアウト例:
`web/config-3boards.json`（`?config=config-3boards.json` で読込）。

- ブローカーは33msごとにライブイベントをバッチして1フレームで送る
- 接続直後、ブローカー側5分リングの内容を同形式でバックフィル（4096件/フレーム）

### 制御（WSテキストフレーム、JSON）

```
{"t":"msg","pic":"pic01.png","html":"..."}
{"t":"html","html":"..."}
{"t":"se","file":"se1.wav"}
{"t":"sound","time":"60","file":"bgm01.wav"}
{"t":"voice","text":"..."}
{"t":"skydome","file":"texture1.bmp"}
```

ビューアの扱い: msg/html→sandbox iframe（スクリプト既定無効）のトースト、
se/sound→WebAudio、voice→Web Speech API、skydome→背景テクスチャ差替。

## 第3部: 将来のバイナリ v3（設計済み・未実装）

ホスト辞書＋16バイト固定長イベント。高pps時の適応サンプリングとセットで導入予定。
