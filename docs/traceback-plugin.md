# トレースバック プラグインAPI（仕様のみ・実装待ち）

## 経緯

`packter_tc.pl`（InterTrack/SPIE連携のIPトレースバッククライアント）は
**3.0で廃止**（裁定 2026-06-10）。理由: InterTrackプロジェクトの終息、
依存ライブラリ XML::Pastor のメンテナンス終了。

ただしトレースバックの**概念**——「飛翔体を選択 → パケットハッシュを
外部システムに照会 → 経路情報を演出として表示」——はPackterの特徴的
機能なので、ブローカーのプラグインAPIとして将来再実装できる形で仕様を残す。

## データは今も流れている

- Agent `-T <server>` は従来どおり DESCRIPTION 欄に
  `MD5ハッシュ32hex-サーバIP` を埋め込む（agent 3.0でも互換維持）
- ブローカーはこれをイベントの desc としてビューアまで届けており、
  クリック選択で表示できる

## 将来API（案）

```
POST /api/trace
  { "hash": "<32hex>", "server": "<ip>", "desc": "<original record>" }
→ ブローカーが登録済みトレースバックプラグインに問い合わせ
→ 結果を該当クライアントに JSON フレームで返す
  { "t": "trace-result", "ok": true, "path": [...], "html": "..." }
```

プラグインは外部コマンド or HTTPエンドポイントとして設定（`--trace-plugin URL`）。
旧 packter_tc の演出キー（IPTB_START_*, IPTB_SUCCEED_*, IPTB_FAILED_*）は
このプラグイン応答の表示に流用する。

実装トリガー: 照会先となる実システムが現れたとき。
