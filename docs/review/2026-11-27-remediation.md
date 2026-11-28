# Swiftify 実機導入前の修正台帳

2026年11月27日（JST）。監査基準の上流と Fork の `main` はともに
`51dd3efda762fd739d68e872eb01754bb8c4e67d`。元の指摘と既存の Linux
検証結果は Draft #16 にある。[監査報告](https://github.com/kazaminosuke/swiftify/blob/62683fa4e37c52ad78f0bffd6c22f35f5709423c/docs/review/2026-11-27.md) · [初回検証](https://github.com/kazaminosuke/swiftify/blob/62683fa4e37c52ad78f0bffd6c22f35f5709423c/docs/review/2026-11-27-verification.md)

## 監査の指摘

| ID | 状態 | 対応と残る確認 |
| --- | --- | --- |
| A01 | 修正済み | #14 の有界 PCM キューを統合。ファイルの読み取り、デコード、DSP を出力コールバックから分離。 |
| A02 | 修正済み | #14 の DSP 後のメディア時刻と CPAL の提示推定を Qt と歌詞の時計に接続。物理機器の伝達遅延は実機項目。 |
| A03 | 修正済み | #14 の終端通知は再生中の PCM キューと提示完了にひも付く。 |
| A04 | 修正済み | #14 の非同期ローカルシークと世代管理を統合。 |
| A05 | 修正済み | #14 の出力形式変換は事前確保された領域を使用。 |
| A06 | 部分修正 | #14 で Spotify の出力が書き込み側を待たず、解放待ち領域を事前確保。混雑中の無音、librespot のクロスフェードと実機出力を検証する余地がある。 |
| A07 | 部分修正 | #11 の Reader キャンセルと本修正の ABI ハンドル破棄時キャンセルを統合。本体のメディア入力はローカルファイル。独立クレートの MediaProvider は同期 I/O に対応しており、実装側で停止と期限を保証する必要がある。OS のブロッキング読み取りは終了を待つ場合がある。 |
| A08 | 部分修正 | 音声トラックの duration とトリム済みフレーム数を優先。複数トラックで選択音声の長さが不明なら、セグメント全体の長さを表示しない。生成した WebM で既定トラックの再生を検証。Symphonia 0.6.1 と現行上流が Matroska の `DiscardPadding` をデコーダーへ渡さないため、同フィクスチャでは末尾を含め約 19 ms 長くデコードする。[上流のデマルチプレクサー](https://github.com/pdeljanov/Symphonia/blob/331bfb18d3b4c300f4136fc0c3ffdb7065505a96/symphonia-format-mkv/src/demuxer.rs#L520) |
| A09 | 部分修正 | ローカル再生の MPRIS Rate は実際のテンポに追従。単調時計とシーク世代で `Seeked` を通知。ローカルパスから Spotify URL を生成しない。Spotify の外部プレーヤー時計と Hyprland 側の表示は実機確認が残る。 |
| A10 | 部分修正 | #10 の独立エンジンのデバイス選択、#11/#14 の Qt 出力復旧を統合。Qt の出力先は OS の既定デバイス。PipeWire の USB/Bluetooth、サンプルレート切替とアプリ内選択の要否は実機で確認する。 |
| A11 | 修正済み | #14 の PCM プロデューサーが曲末で DSP をフラッシュし、提示完了を待つ。 |
| A12 | 修正済み | #14 の Core ハンドルごとのローカル出力所有を統合。 |
| L01 | 修正済み | #13 の Enhanced LRC・TTML 単語同期と TTML サイドカー編集を統合。 |
| N01 | 修正済み | Spotify API は固定オリジン、リダイレクト停止、4 MiB・30 秒、200 ページ・180 秒、同時 8 件。画面遷移、切断と Retry-After を処理。 |
| N02 | 修正済み | 画像オリジンをサービス別に制限。リダイレクトを停止し、受信 8 MiB・20 秒、同時 4 件、展開 32 Mi ピクセル、ローカルファイル 32 MiB で制限。 |
| N03 | 修正済み | YouTube Data API は 2 MiB・25 秒、1 画面 40 ページ、ページと本人情報を各 1 件。401、403、429 と手動再試行を管理。 |
| N04 | 修正済み | Core 完了通知で再入 reset を受けたときに旧世代の drain を終了。破棄時はネイティブ要求をキャンセル。Qt のテストで完了からの reset と、Spotify API とネットワーク応答の破棄後に再開する中断処理を検証。 |
| Q01 | 修正済み | #7/#12 のスキャン中に追加された保持コピーを再照合する動作を維持。 |
| Q02 | 修正済み | Corrosion、QtKeychain、QCoro のコミットを固定。Qt Windows MSVC キットと Linux 配布ツールの SHA-256 を検証し、`--locked` と依存グラフを配布物に記録。 |
| Q03 | 部分修正 | Qt 6.12.1 の Xvfb で、従来失敗していたアイコン 4 件と狭幅レイアウトを再検証。テストはウィンドウの露出と寸法変更の反映を待つ。CachyOS 上で幅、スケーリングとフォント描画の確認が残る。 |
| R01 | 修正済み | #14 でデバイス出力と独立したサンプルレートで解析し、シークで世代を切り替える。 |
| R02 | 修正済み | #14 で Spotify 出力の解放領域をライター側で確保し、キュー全体を排出可能にした。 |

## 追加のセキュリティ項目

| 対象 | 状態 | 対応と残る確認 |
| --- | --- | --- |
| Spotify の選択 | 修正済み | 通常起動はローカル GUI。`--enable-spotify` か `SWIFTIFY_ENABLE_SPOTIFY=1` を指定したプロセスで復元と Soloist を有効にする。`--disable-spotify` が優先。ビルド無効構成の Cargo グラフから librespot、Spotify OAuth、RSA が外れる。 |
| Google の資格情報と割当 | 修正済み | 公開データは API キー、本人データは読み取り専用 OAuth。401 ではセッションを無効化し、権限 403 は対象資格情報の通信を停止。割当と 429 は両経路を休止し、Retry-After と手動再試行を尊重する。 |
| Google の更新と失効 | 修正済み | オンラインアクセスが既定。オフラインアクセスは明示指定で、強制同意を送らない。期限切れ後の本人操作で更新し、切断時に通信とローカル状態を止めて Google の失効結果を表示する。 |
| Google プロジェクト | 外部設定待ち | Desktop OAuth クライアントの公開状態、スコープ審査、テストユーザー、API キー制限と有効な割当をプロジェクト管理者が確認する。[設定手順](../youtube-catalogue.md) |
| タグ編集 | 修正済み | 同一ディレクトリに排他的に一時ファイルを作り、開いた記述子で更新。元ファイルの状態を照合し、完了時にアトミックに置換。 |
| プレイリストとカバー | 修正済み | 格納名、絶対パス、正規化と格納先を検証し、シンボリックリンクをまたぐ管理ファイル操作を拒否。古いローカルプレイリストの移行を維持。 |
| ローカルスキャン | 修正済み | リンクをたどる走査を抑止し、項目数、曲数、メタデータ量、処理時間を制限。終了時のキャンセルを追加。OS とストレージの同期操作は個別の締切で強制停止できない場合がある。 |
| Discord | 修正済み | ローカル曲は再生・一時停止状態のみを送る。タイトル、アーティスト、アルバム、カバー、曲長、再生位置は送らない。 |
| デスクトップ IPC | 修正済み | ユーザー専用の接続とロックで起動を調停。Linux は両方をユーザーのランタイムディレクトリに置く。接続 8 本、40 バイト、1 秒。起動時に明示した Spotify モードが稼働中のプレーヤーと異なる場合は終了して知らせる。 |
| Spotify の認証と通信 | 修正済み | 状態を検証したループバック交換、期限とサイズ制限、リダイレクト停止、エラーの秘匿化を適用。切断時に Web API、OAuth と Core の要求を停止。実アカウントによる互換確認は対象外。 |
| Alignment Lab | 保留 | #15 の独立構成。追加開発、モデルの再選定・ベンチマーク、ワーカー隔離とモデル読み込みは別作業。 |
| Google 認証方式・YouTube Music 再生 | 保留 | 公式 OAuth と YouTube Data API を維持。Stream Tube Desktop と MusicBeatSP との比較および再生方式の決定は別作業。 |

## 依存関係

| 依存関係 | 状態 | 選んだ版・確認内容 |
| --- | --- | --- |
| Qt | 修正済み | 6.12.1 を最低要件にした。Qt Network の CVE-2026-116337 と Qt WebSockets の CVE-2026-114366 の修正を含む。同等の修正が入った旧系でのビルドは今回未検証。[Qt 6.12.1](https://www.qt.io/blog/qt-6.12.1-released) · [Qt Network 勧告](https://www.qt.io/blog/security-issue-cve-2026-116337?hsLang=en) · [Qt WebSockets 勧告](https://www.qt.io/blog/security-issue-cve-2026-114366) |
| quick-xml | 修正済み | 0.42.0。修正最小版 0.41.0 以降。vendored librespot の XML API 呼び出しを適合。[RustSec 0194](https://rustsec.org/advisories/RUSTSEC-2026-0194.html) · [RustSec 0195](https://rustsec.org/advisories/RUSTSEC-2026-0195.html) |
| tokio | 修正済み | 両ロックを 1.54.1 に更新。[RustSec 0384](https://rustsec.org/advisories/RUSTSEC-2026-0384.html) · [RustSec 0386](https://rustsec.org/advisories/RUSTSEC-2026-0386.html) |
| rustix | 修正済み | 1.x を 1.1.6 に更新。0.38 系は当該勧告の非該当範囲。[RustSec 0486](https://rustsec.org/advisories/RUSTSEC-2026-0486.html) |
| url | 修正済み | 2.5.9。[RustSec 0592](https://rustsec.org/advisories/RUSTSEC-2026-0592.html) |
| rsa | 修正済み | 修正版の安定版 0.10.0 に移行。Spotify の用途は AP の公開鍵検証で、旧構成も秘密鍵の処理に使っていなかった。Spotify 無効構成からは外れる。[RustSec 0071 / CVE-2023-49092](https://rustsec.org/advisories/RUSTSEC-2023-0071.html) |
| chrono | 部分修正 | 0.4.45。Spotify の OAuth 依存で、安定版を確認。保守終了警告が残る。Jiff への移行は oauth2 側の日付型変更と互換検証が必要。[RustSec 0543](https://rustsec.org/advisories/RUSTSEC-2026-0543.html) |
| paste | 部分修正 | 1.0.15。Lofty が使うビルド時の proc macro。pastey は置換候補。Lofty 側の更新を優先し、現時点の保守終了警告を記録する。[RustSec 0436](https://rustsec.org/advisories/RUSTSEC-2024-0436.html) · [pastey](https://crates.io/crates/pastey) |
| 配布物 | 修正済み | `share/licenses/swiftify/build-provenance.txt` に機能フラグ、Qt、コンパイラ、依存の出典、Cargo.lock の SHA-256 と有効な依存グラフを収録。Rust の配布ライセンスも再生成。 |

RustSec advisory-db `c5279d0` を使い、`cargo-audit 0.23.0` で両方の
ロックファイルを再監査した。脆弱性の一致は 0 件。Spotify Core の
`chrono` と `paste` に保守終了警告があり、Native Audio は警告 0 件。

## 検証結果

Debian 13 x86_64、Qt 6.12.1、GCC 14.2、Rust 1.100.0 で Debug ビルドを確認した。

- `ENABLE_SPOTIFY=ON/OFF` の両方をビルド。対象の Qt CTest は有効 14/14、無効 12/12。Google と Spotify の認証、公開用と本人用の資格情報、401／403／429、ページング、中断と失効はモックとダミー資格情報で検証した。Spotify のセッション試験は 3 回追加実行し、各回 36 件が通過。
- 両構成で Xvfb の `tst_views` が通過。Qt 6.10.3 の比較で失敗していたアイコン 4 件と狭幅レイアウト、Spotify 無効時にアカウント画面が表示されないことを確認した。
- Wayland の Weston で両構成を新規プロファイルから起動。生成 WAV の再生と MPRIS の位置の進行、同時起動の調停、IPC のサイズ・時間制限、未設定の YouTube 画面を確認。Spotify を無効にした起動と再生、未設定の YouTube 画面のトレースでは IPv4／IPv6 ソケットが 0 件。保存済み資格情報に対するアクセスと通信がないことは無効セッションのモックで確認した。
- Rust C ABI は Spotify 有効 75 件、無効 54 件が通過。両構成で仮想 ALSA を使い、別枠の再生時計・DSP・一時停止中シークも各 1 件通過。Release の解析性能ベンチマークは対象外。
- 独立 Native Audio は単体 5 件、生成音源のデコード・シーク 2 件、仮想 ALSA の出力・復旧 3 件が通過。
- `ldd` と機能別の Cargo 依存グラフで、無効構成から Qt WebSockets、QtKeychain、librespot、Spotify OAuth、RSA が外れていることを確認した。
- 両構成の Linux インストールを一時ディレクトリにステージし、依存グラフと出典のマニフェストを確認した。
- GitHub Actions の手動ディスパッチは HTTP 422 を返した。リポジトリで Actions が無効のため、GitHub 上の結果と各 OS のリリース用パッケージ実行結果は未確認。

## Draft の依存と実機条件

上流追従のために、[統合 #17](https://github.com/kazaminosuke/swiftify/pull/17)
を [Google #18](https://github.com/kazaminosuke/swiftify/pull/18)、
[Spotify #19](https://github.com/kazaminosuke/swiftify/pull/19)、
[本体の境界 #20](https://github.com/kazaminosuke/swiftify/pull/20)、
[依存関係と確認結果 #21](https://github.com/kazaminosuke/swiftify/pull/21) の基準にした。
#17 は既存 #8、#13、#14 とその前提をまとめた検証用の差分。元の Draft はそれぞれ
既存の base と head を維持する。

実機導入の判定には CachyOS / Hyprland の Wayland での表示、フォント、ファイルピッカー、
MPRIS、PipeWire 出力、USB/Bluetooth の復旧、44.1/48 kHz と歌詞シークの確認が残る。
Google を使う場合は Cloud プロジェクトの外部設定も確認する。
