# Packter 2.x → 3.0 移行ガイド

## いちばん大事なこと: エージェントは無改修で動く

旧 **PackterAgent 2.5**（`pt_agent` / `pt_sflow` / `pt_netflow`）は、送信先を
**ブローカー**の UDP 11300 に向けるだけでそのまま使えます。ワイヤフォーマットは
完全互換です。

```sh
# 旧来どおり。-v をブローカーのIPにするだけ
pt_agent -v <broker-ip> -i eth0
```

ビューアは旧 XNA 版（Viewer 2.4）の代わりに、ブローカーが配信する **Web ビューア**を
ブラウザで開きます。

## 構成の対応

| 2.x | 3.0 |
|---|---|
| PackterViewer 2.4（XNA/Windows） | Web ビューア（ブラウザ、ブローカーが配信） |
| Viewer が UDP 11300 を直接受信 | **ブローカー**が UDP 11300 を受信し WebSocket で配信 |
| `pt_thmon` 単独プロセス | ブローカー `--thmon packter.conf`（MON_*キー互換、しきい値は `TH_*`） |
| Snort `-A unsock`（`pt_agent -U`） | 引き続き利用可。加えてブローカー `--eve eve.json`（Suricata） |
| `packter_tc.pl`（InterTrackトレースバック） | **廃止**。概念はプラグインAPIとして仕様のみ温存（[traceback-plugin.md](traceback-plugin.md)） |
| IPトレースバックの `-T`（ハッシュ埋め込み） | 互換維持（descに `hash-server`、ビューアで選択表示） |

## ビューア設定（Config.txt → config.json）

旧 `Config.txt`（Shift_JIS、`Key = Value`）は、Web ビューアの `web/config.json` に
置き換わりました。主な対応:

| 旧 Config.txt | 新 config.json | 備考 |
|---|---|---|
| `Size` | `size` | パケットの大きさ倍率 |
| `BindPort` | （ブローカー `--udp`） | 受信はブローカー側 |
| `XAxisStart/End`, `YAxisStart/End` | `axis.xStart/xEnd/yStart/yEnd` | IPv4文字列・0–1いずれも可 |
| `SenderBoardFile` 等 | `boards[].name` ほか | 配置は自動（円状）。位置の手指定は不要に |
| `EnableSkyDome`, `SkyDomeTexture` | `skydome` | URL/パス |
| `MaxSENum` | `maxSe` | SE同時再生数 |
| `MsgBoxCloseTimeSec` | `toastMs` | ミリ秒 |
| `HtmlConvertTarget` 系 | テンプレート変数 `{{ASSET_URL}}` `{{VIEW_WIDTH}}` `{{VIEW_HEIGHT}}` | PACKTERMSG/HTML 内で展開 |
| `ProgramMode`(Ballistic等) | 飛翔体ヘッダで指定（`PACKTERBALLISTIC`） | レコード単位 |

## 廃止・変更された機能

- **XACT 音響** → WebAudio（wav直接再生）に一本化。SE/BGM音源は AudioCraft で再生成予定
  （現状は合成プレースホルダ、`web/assets/sound/RECIPES.md`）
- **PACKTERVOICE** の外部音声合成（softalk起動）→ ブラウザの Web Speech API
- **PACKTERHTML** → サンドボックス iframe で表示（スクリプト既定無効）。送信元は
  ブローカーに到達できる主体に限定（`--agent-key` / `--require-auth` で境界を強化）
- **VR**（2017 Vive版）→ いったん無効化（必要なら WebXR を再追加可能）
- **Gateway 軌道**（PACKTERWITHGATEWAY）→ プロトコルは受理するが直線描画（ビューア未実装）

## 同一ホストで複数エージェント

旧来は送信元IPで区別していましたが、同一ホストに複数エージェントを置くと区別できません。
3.0 では `pt_agent -A <id>` で **AgentID** を付け、ブローカー `--agent <id>=<board>` で
ボードに割り当てます。`-K <pskfile>` を足すと HMAC-SHA256 認証になり、制御コマンド注入
（XSS）への対策にもなります。
