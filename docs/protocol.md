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

ボード割り当ては **AgentID優先 → 送信元IPルール → 既定0** の順:
```
packter-broker --board 192.168.1.5=2 --board 10.0.0.0/8=3   # IPで振り分け
               --agent sflow1=2 --agent pcap1=0             # AgentIDで振り分け
               [--eve-board 4]
```
IPルールは「IP=index」（v4/v6完全一致）または「CIDR=index」（v4）。
ボード数は `--boards N`（明示）かルールからの自動導出（最大index+1、最低2、
上限16）。ブローカーは `{"t":"layout","count":N}` を配信し、ビューアはN枚を
**地面（XZ平面）に円状に立つ縦の壁**として配置する。受信ボード（board index
1＝飛翔体の宛先）が奥、送信ボードが手前に扇状に並ぶ。真上から見ると壁は棒に
見え、三角形・四角形・五角形“状”に並ぶ（辺は閉じない・隙間OK・立体ではない）。
パケットは各エージェント壁から受信壁へアリーナ空間を横断して飛ぶ（攻撃/トラフィック
フローの可視化）。

**エージェントの選別（無限増殖対策）**:
- `--strict` を付けると、ボードに割り当たらないソース（`--agent` 未登録かつ
  `--board` ルール不一致）は**ドロップ**される。`--agent` が実質の許可リストになる。
  付けない場合は未登録ソースは board 0 に集約（従来動作）
- ビューアは数字キー 1–9 でボードをライブに非表示/再表示でき、隠したボードに
  出入りするパケットも抑止される。運用の恒久選別は `--strict`、その場の探索は
  ビューア側、と使い分ける

### AgentID と認証（同一ホスト対策＋XSS対策）

同一ホストから複数エージェントを動かすとIPで区別できないため、データグラム
先頭に **AgentID行**を付加できる（Agent `-A <id>`、付けなければ従来どおり）:
```
PACKTERAGENT <id>                          識別のみ
PACKTERAGENT <id>,<unix時刻>,<HMAC64hex>   認証つき（Agent -A <id> -K <pskファイル>）
```
- HMAC-SHA256 は PSK をキーに `"<id>,<時刻>\n" + データグラム残り全文` を署名。
  ブローカーは ±300秒のリプレイ窓で検証
- ブローカー設定: `--agent-key <id>=<pskファイル>`（鍵はファイルから読む。
  CLI直書きしないことでps漏洩を回避）。`--require-auth` で匿名/未認証を全拒否
- **鍵が1つでも設定されると、制御コマンド（MSG/HTML/SE/SOUND/VOICE/SKYDOME）は
  認証済みエージェントからのみ受理される** → UDP 11300到達だけでHTMLを注入できる
  XSS経路を封鎖。fly イベントは匿名でも従来どおり受理（後方互換）
- `--forward`（レガシー転送）時は PACKTERAGENT 行を**剥がして**送るので旧Viewer
  2.4 は無影響

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
{"t":"board","index":0,"label":"border-router"}
```

`board` はボードのキャプション更新。エージェントが `-A <id>` を付けると、
ブローカーはそのエージェントが割り当たるボードへ `id` をキャプションとして
配信する（変化時のみ。新規接続クライアントには現在値をリプレイ）。ビューアは
ボード面に焼き込まれた文字ではなく、このラベルをスプライトで表示する。
内部のボードインデックス（0=sender, 1=receiver…）は不変で、表示文字だけが変わる。

ビューアの扱い: msg/html→sandbox iframe（スクリプト既定無効）のトースト、
se/sound→WebAudio、voice→Web Speech API、skydome→背景テクスチャ差替。

## 第3部: 将来のバイナリ v3（設計済み・未実装）

ホスト辞書＋16バイト固定長イベント。高pps時の適応サンプリングとセットで導入予定。
