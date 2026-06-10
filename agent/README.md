# PACKTER Agent 3.0

PackterAgent 2.5 の後継。**ワイヤフォーマットは完全互換**（Viewer 2.4 にもそのまま送れる）。

## 2.5からの変更点

構造:
- autotools → 素のMakefile（`make` だけでビルド。依存は libpcap のみ — glib2 / OpenSSL 依存を削除。MD5は内蔵実装）
- `extern` グローバル15種 → `packter_ctx` 構造体（`include/packter.h`）
- プロトコル別7ファイル（pt_tcp/udp/icmp/icmp6/ip/ip6/datalink）→ `lib/proto.c` に統合
- 送信・設定・権限降格などツール間で重複していた19関数 → `libpackter.a` に集約
- IPv6 は `--enable-ipv6` 不要（`-v` のアドレス形式から実行時判定、`#ifdef USE_INET6` 33箇所を撤去）

新機能:
- `-B <ms>` バルク送信: 複数レコードを1データグラム（MTU内）にまとめる。Viewer 2.4も受理する正規形バルク
- `-g <group>`: 2.5ではgetoptに存在したが未実装だったグループ降格が動作

修正したバグ（出力が変わるもの）:
- IPv6パケットが一切出力されなかった（`p + payload_length` への誤ジャンプ）→ 拡張ヘッダ走査つきで修正。flag 5–9 が初めて実際に流れる
- ループバック(DLT_NULL)キャプチャが `userdata==NULL` チェックで常に無処理だった → 修正
- `sprintf` 自己重複の未定義動作（Snort連携時のTCP説明文）→ 一時バッファ経由
- スナップ長を超えるパケットで `h->len` 分読んでいたOOB read → `caplen` 基準に
- pt_thmon: MON_OPT_SOUND_HEAD/FOOT が voice バッファに連結されていた → sound に修正
- pt_netflow: TCPフラグのフィールドオフセット判定が `== PACKTER_TRUE(1)` だった → 欠落判定を分離。1フローセット内の複数レコードも全件処理（2.5は先頭のみ）
- `pcap_compile` のBPFプログラム未解放、設定の重複キーリーク

## ビルドとテスト

```
make            # pt_agent / pt_replay / pt_sflow / pt_netflow / pt_thmon
make test       # ユニット21件 + ゴールデンクロスチェック
make SANITIZE=1 # ASan/UBSan ビルド
make GEOIP=1    # レガシーlibGeoIPで -G を有効化（任意）
```

ゴールデンテスト（`tests/`）は、レコード整形規則とトレースバックMD5を**Pythonで独立に再実装**した期待値（`make_pcap.py`）と、`pt_agent -n` の出力をバイト単位で突き合わせる。2.5のIPv6ハッシュの癖（先頭バイトのみマスク）も再現済み。

実ブローカーへの送信スモークは `tests/e2e.sh`。
