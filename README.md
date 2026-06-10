# PACKTER 3.0 (alpha)

インターネットトラフィック可視化ツール PACKTER の現代化版。

- **broker/** — Rust製ブローカー。旧Packterプロトコル（UDP 11300）を互換受信し、
  33msごとのバイナリバッチをWebSocketで配信。Webビューアの静的配信も担当
- **web/** — Webビューア（Three.js）。レガシー `ball.x` 由来のボール（陰影つき）が
  flag色で飛ぶ。3軌道（直線/弾道/ゲートウェイ経由）、クリック選択、
  PACKTERMSG/HTMLトースト（sandbox iframe）、WebAudio効果音、Web Speech音声、
  スカイドーム実行時差替、WebXR（VRボタン）、直近5分の巻き戻し再生。
  操作: S=停止 / C=LIVE / B,F=コマ送り / Backspace=-5分 / Space=HUD / Alt+Enter=全画面。
  設定は `web/config.json`（サイズ・flag色・ボード・軸レンジ・地形glTF等）
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
