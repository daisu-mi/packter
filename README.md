# PACKTER 3.0 (alpha)

インターネットトラフィック可視化ツール PACKTER の現代化版。

- **broker/** — Rust製ブローカー。旧Packterプロトコル（UDP 11300）を互換受信し、
  33msごとのバイナリバッチをWebSocketで配信。Webビューアの静的配信も担当
- **web/** — Webビューア（Three.js）。flag色の飛翔体、sender/receiverボード、
  旧版スカイドームテクスチャ、直近5分の巻き戻し再生（S=停止 / C=LIVE / B,F=コマ送り / スライダー）
- **tools/** — テストトラフィック生成（sender.py）
- **docs/** — プロトコル仕様・プロジェクト系譜

## クイックスタート

```
cd broker
cargo run --release -- ../web      # UDP 11300 で受信、http://localhost:11380/ でビューア
python ../tools/sender.py --pps 300   # テストトラフィック
```

既存の PackterAgent 2.5（pt_agent / pt_sflow / pt_netflow）は無改修でそのまま使えます:

```
pt_agent -v <brokerのIP> -i eth0
```

## 互換性

ブローカーの互換パーサは以下をすべて受理します（寛容受信）:

- `PACKTER\n` ＋ レコード列挙（正規形バルク）
- `PACKTER\nレコード` の組の繰り返し
- `PACKTER レコード`（1行形式）
- 旧 `PACTER` ヘッダ、`PACKTERBALLISTIC`、`PACKTERWITHGATEWAY`
- 座標欄: IPv4 / IPv6 / 正規化座標(0–1) / 整数(1–65536)

リファクタリング全体計画は `C:\packer\PACKTER-REFACTORING-PLAN.md` を参照。

## ライセンス

コード: BSD 2-Clause。アセット（スカイドーム・flag色・ボードテクスチャ）は
旧Packterプロジェクト由来（CC BY、クレジットは docs/HISTORY.md 参照）。
