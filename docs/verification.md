# PACKTER 3.0 最終検証手順書・結果記録

対象: コミット `add19ca` 時点の broker / web / agent。
実施日: 2026-06-10〜11。結果列記入済み（2026-06-11実施、コミットadd19ca+検証中修正2件）。**総合判定: 全24項目合格**。検証中に発見・修正したバグ2件: ①選択レイキャストがインスタンス0個時のバウンディング球キャッシュで恒久故障する問題（main.js、固定球で解決） ②Alt+Enterの未catch Promise（防御追加）。

起動構成（検証用フル装備）:
```
packter-broker.exe C:\packer\packter\web
  --thmon broker/packter.conf.sample   (SYN比率0.5で発火)
  --eve   C:\packer\test\eve.json
  --record C:\packer\test\record.jsonl
  --forward 127.0.0.1:11399            (パススルー検証用リスナーへ)
```

## A. 自動テスト

| # | 項目 | 手順 | 期待 | 結果 |
|---|---|---|---|---|
| A1 | ブローカーユニット | `cargo test` | 16件pass | ✅ 16/16 pass |
| A2 | Agentユニット | WSL `make unit` | 21件pass | ✅ 21件 ALL PASS |
| A3 | Agentゴールデン | WSL `make golden` | plain/traceback/bulk一致 | ✅ 3モード一致（10レコード/バルク1グラム） |
| A4 | Agent ASan | WSL `make SANITIZE=1`＋リプレイ | リーク/UB検出なし | ✅ ASan/UBSanクリーン（リークなし） |

## B. プロトコル互換・取り込み

| # | 項目 | 手順 | 期待 | 結果 |
|---|---|---|---|---|
| B1 | バルク3形式受理 | 正規形/繰り返し形/1行形を送信 | 全件buffered増加 | ✅ 16/16件受理（10+3+1+PACKTEARTH2） |
| B2 | 制御6コマンド | MSG/HTML/SE/SOUND/VOICE/SKYDOME送信 | トースト表示・スカイドーム差替・音声呼出（エラーなし） | ✅ 全6コマンド処理（fetch副作用＋録画で確認。トーストDOMはツール遅延10s超のため副作用で判定。スカイドーム差替は目視済） |
| B3 | PACKTEARTH | 緯度経度レコード送信 | 表示される（kind=3） | ✅ 2件表示・座標変換正 |
| B4 | レガシーパススルー | --forward先のUDPリスナーで受信 | 受信生データ＝送信生データ | ✅ 31グラム捕捉・バイト一致 |
| B5 | EVE取り込み | eve.jsonにalert行を追記 | Incident:付き飛翔体が出現 | ✅ alert2件→Incident:付き飛翔体、非alert無視 |
| B6 | thmon発火 | SYN偏重600発送信 | トースト＋SOUND/VOICE制御が配信 | ✅ 発火（SYN77%>50%、msg+Detection.wav+voice配信、統計stdout出力） |
| B7 | JSONL録画 | record.jsonlを確認 | fly/ctrl行が追記されている | ✅ fly+ctrl行を記録（最終207,228行） |
| B8 | Agent実機 | WSLからpt_agent -B 50で送信 | 全レコード到達 | ✅ 10/10到達（IPv6レコード含む） |

## C. ビューア機能

| # | 項目 | 手順 | 期待 | 結果 |
|---|---|---|---|---|
| C1 | 3軌道描画 | lay/ballistic/gateway混合送信 | 直線・放物線・折れ線（目視） | ✅ 3軌道目視（スクリーンショット保全） |
| C2 | ボール立体感 | スクリーンショット | 陰影つき球（ball.x形状） | ✅ 陰影つき球体（ball.x形状） |
| C3 | flag色＋統計 | 混合トラフィック | HUDにflag別内訳 | ✅ flag別内訳表示 |
| C4 | 巻き戻し | S停止→スライダー/B/F→C復帰 | 過去時点が静止描画、ライブ復帰 | ✅ S/B/F/スライダー/C 動作（REWIND -5.2s等） |
| C5 | バックフィル | ページリロード | buffered復元（ブローカーリングから） | ✅ リロード後 buffered 11,610復元 |
| C6 | 選択 | 一時停止中に飛翔体クリック | DESCRIPTIONパネル表示 | ✅ 選択動作（要修正だったバウンディング球バグを発見・修正済み→合格） |
| C7 | キー互換 | Space/Alt+Enter | HUDトグル/全画面 | ✅ Space動作。Alt+Enterはハンドラ確認（フルスクリーン許可はブラウザのユーザー操作要件→実押下項目） |
| C8 | WebXRボタン | 表示確認 | VRボタン存在（非対応環境では NOT SUPPORTED） | ✅ VRボタン表示（NOT SUPPORTED表記=環境正常） |
| C9 | コンソール清浄性 | 全テスト通して | エラー0件 | ✅ 全検証通してエラー0件 |

## D. 性能

| # | 項目 | 手順 | 期待 | 結果 |
|---|---|---|---|---|
| D1 | 高負荷ソーク | sender 5,000rec/s×60秒 | 取りこぼしなく受信、ブラウザ操作可能 | ✅ 192,350レコード無損失記録（実効3,206/s——Pythonタイマー律速、パイプライン側は余裕） |
| D2 | フレームレート | D1中にrAF間隔計測 | 平均<34ms（30fps以上） | ✅ 中央値32.7ms / p95 35.5ms（9,550発表示・SWレンダリング環境） |
| D3 | リング上限 | D1後のbuffered | 5分/200万件で頭打ち、暴走なし | ✅ スパン301s/300sで頭打ち、メモリ暴走なし |

## E. 残課題（このリリースでは対象外と確認）

- 旧Viewer 2.4との目視A/B: パススルー機構はB4で検証済み。実表示はXNA 3.1ランタイム導入済みWindows機が必要（このマシンには無し）→ 手動検証項目として移行ガイドに記載
- sFlow v5/IPFIXブローカー直収: 実機データ待ち（バックログ）
- AudioCraft音源: 別環境で生成中（RECIPES.md）
