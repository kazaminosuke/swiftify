# 2026-11-27 修正・検証記録

修正前のコードの参照は [レビュー](2026-11-27.md) にある。
検証対象の Draft は [歌詞 #13](https://github.com/kazaminosuke/swiftify/pull/13)、
[再生と時計 #14](https://github.com/kazaminosuke/swiftify/pull/14)、
[Alignment Lab #15](https://github.com/kazaminosuke/swiftify/pull/15)。

| ID | 対応 |
| --- | --- |
| A01・A02・A03・A04・A05 | #14。PCM キュー、CPAL 提示推定、ソース単位の EOF、Qt からの非同期シーク、確保済み出力変換に変更。旧 ABI 呼び出しは残し、新しいスナップショットと非同期シークを追加。 |
| A06 | #14。既存 Spotify 出力もコールバックで書き込み側を待たず、解放待ち領域を事前確保。クロスフェードのパケット経路は引き続き独立。 |
| A11・A12 | #14。ローカル EOF のオーバーラップ出力と、Core 単位の出力所有権。 |
| R01 | #14。既存リサンプラーを解析にも使用し、デバイスレートが変わっても解析レートを維持。 |
| R02 | #14。既存の長いキューを先に積むテストで検出し、書き込み側で解放待ち領域の内容を解放し、キュー全体を収容できる容量で再利用する。 |
| L01 | #13。Enhanced LRC / TTML、両歌詞パネルでの単語同期、ローカル編集のアトミックな TTML サイドカー。 |
| Q01 | 既存 #7/#12 の修正を統合検証。 |
| A07–A10・N01–N04・Q02–Q03 | 残る項目と根拠はレビューの各行を参照。Q03 はヘッドレス条件を含む比較結果を下記に記録。 |

## 再生

CPAL が報告するコールバック時刻と提示予定時刻を使い、DSP 後の PCM と録音上の位置を結びます。
フレームが届かない期間は提示時計を進めません。遅延情報が得られないデバイスでは、送出済み位置が
フォールバックです。ネットワーク、Bluetooth、物理スピーカーの伝達遅延全体は計測していません。
既存 Spotify ストリーミングと Soloist の外部メディア時計はそれぞれの経路を使います。

## 実測環境

Debian 13 Linux x86-64、Qt 6.10.3、Rust 1.100.0。仮想 ALSA デバイスを使用。
CachyOS / Hyprland の物理出力と体感、Windows/macOS、実アカウントでの OAuth は未検証。

- Rust ネイティブユニット5件、コーデック1件、ALSA出力3件。
- Rust Core C ABI ライブラリ75件、試験用の2件は通常実行から除外。うち新規の ALSA 時計テストは個別に成功。
- ネイティブクレートの rustfmt、Clippy（警告をエラー扱い）を通過。
- Alignment Lab は Qt Quick の独立ビルド、Python ユニット6件、GUI のワーカープロトコル試験1件。
  CPUベンチマークは15ペア・45試行。Qt の型付き QML を qmllint で確認。CUDA デバイス未検証。

- Rust Core の変更と #8/#11/#12/#13/#14 を Debian 上で統合し、
  Qt のプレーヤーと YouTube ワークスペースをビルドした。Linux デスクトップ統合は無効。
  両モードのオフスクリーン起動と
  GPL、依存告知、Linux デスクトップファイルのステージングを確認した。
- 最終検証用ヘッド `dc86c06` の Xvfb CTest は22/23。
  `tst_views` の `filledIconsHaveNoHoles` 4件（account_circle、library_music、home、favorite）と
  `aNarrowWindowClosesItsRightPanels` が失敗。最初の統合実行では狭幅テストは成功しており、
  初回も22/23。最終実行の5件は #12 にある上流比較の失敗項目と一致した。
- GitHub Actions の手動ディスパッチは HTTP 422。リポジトリで Actions が無効という応答だった。

## 実機で確認する項目

- CachyOS / Hyprland の Wayland セッションでプレーヤーと Alignment Lab を開き、
  ドラッグ、歌詞エディター、ファイルダイアログ、スケーリングと MPRIS を確認。
- PipeWire の通常出力、USB/Bluetooth の切断と手動復旧、44.1/48 kHz の切替。
  一時停止とシーク、曲末の自動送り、テンポと EQ の変化、同期歌詞の聞こえ方を確認。
- フル尺の伴奏付き日本語歌唱で、伸ばし音、繰り返される歌詞、混声、特殊な漢字読みを確認。
  利用可能なら ONNX CUDA、VRAM、Swiftify と Lab が同時起動した際の操作を確認。


## Codex 利用残量

公式 App Server `account/rateLimits/read` を実行。
2026-11-27 07:50、08:07、08:15、08:21、08:31、08:36、08:45 JST の確認では HTTP 401 が返った。
割合は返らず、10%基準を判定できる値は得られなかった。
