# YouTube Music バックエンドの技術選定記録

2026年11月28日の調査。候補は固定リビジョンのソース、ビルド定義、配布ライセンスと関連 Issue を確認した。現時点の第一候補は、YouTube Music 向けの既存 Rust パーサーと、同じサービスのセッションで得られる音声を既存エンジンへ渡す小さな入力アダプターの組み合わせ。実ストリームとアカウント利用を検証できていないため、バックエンドの採用と依存追加は保留している。

## 候補の比較

| 候補 | 実装、依存関係と評価 |
| --- | --- |
| ytmapi-rs | MIT。YouTube Music の検索、アルバム、アーティスト、プレイリスト、ライブラリ、評価と歌詞に型付きクエリーを提供する。Tokio と reqwest を使い、HTTP クライアントを差し替えられる。ホームとリクエストロケールは補完が必要。音声の取得は同じリポジトリ内の youtui が別のダウンローダーで担当し、Refryt もカタログに採用している。ライブラリのページングとパーサーは継続して修正されている。 [クレートとライセンス][ytmapi-manifest] · [クエリー][ytmapi-queries] · [HTTP クライアント][ytmapi-http] · [youtui #396][ytmapi-issue] |
| RustyPipe | GPL-3.0-only。YouTube と YouTube Music の検索、ブラウズ、歌詞、ライブラリ、プレーヤーレスポンスとスナップショットテストがそろう。0.11.4 の実装は QuickJS を含み、Web 再生経路の一部に別プロセスを使う。既定の JSON キャッシュはアカウント情報を含み得るため、直接依存する場合はストレージとレポーターの差し替えが必要。現行ソースの最終コミットは2026年8月3日。 [クレートとライセンス][rp-manifest] · [プレーヤー][rp-player] · [ストレージ][rp-storage] · [リリース][rp-release] |
| caya8205-2/innertube-rs | MIT。ホーム、検索、ライブラリ、アルバム、アーティスト、プレイリストのパーサーは部分移植の候補。既定の再生経路はアクセス拒否後に別クライアントへ切り替える。直接依存では QuickJS、Tokio `full`、Prost と vendored `protoc` も入る。 [クレートとライセンス][caya-manifest] · [音楽ブラウズ][caya-music] · [プレーヤー][caya-player] · [音楽機能の更新][caya-update] |
| Sonora の ytmusic-rs | GPL-3.0-or-later。Music のカタログ、アカウント、署名付き音声の入力を提供する。1 MiB の範囲取得、読取り期限、更新可能なロケーターは参考になる。プレーヤーは別クライアントへの切替やブラウザ経由のトークン生成を含み、セッションは設定次第でファイルに永続化される。Sonora 本体に AAC 再生の実績があり、プレーヤースクリプトの変更に対応する修正も続く。 [クレートとライセンス][sonora-manifest] · [範囲ストリーム][sonora-stream] · [解決処理][sonora-player] · [動作報告][sonora-adoption] · [直近修正][sonora-update] |
| ytm-rs | GPL-3.0-only。egui、Rodio、QuickJS を使うデスクトップアプリ。Premium 用の経路を持ち、ログインには WebView を使う。資格情報保管は OS の保管庫とファイルへのフォールバックを実装する。匿名クライアントがボット判定で再生を拒否された報告があり、別クライアントの正常系だけで安定性を判断できない。 [クレートとライセンス][ytmrs-manifest] · [再生経路][ytmrs-player] · [認証][ytmrs-login] · [保管][ytmrs-vault] · [再生 Issue][ytmrs-issue] |
| Refryt | GPL-3.0-or-later。ytmapi-rs、mpv、匿名のプレーヤー取得と `yt-dlp` の別プロセスを組み合わせる。独立したリゾルバーとタイムアウト設計は参考になる。ネイティブリゾルバーは任意で、認証情報を JSON に保存する実装がある。CDN の応答待ちが再生開始を止めた報告がある。 [ワークスペースとライセンス][refryt-manifest] · [リゾルバー][refryt-chain] · [認証][refryt-auth] · [通信 Issue][refryt-issue] |
| ModernTube の innertube-rs | MIT。前述の同名クレートとは別実装。SABR の VOD とライブを同じライブラリで扱い、シーク可能なセグメント入力を提供する。再生機能は QuickJS とトークンプロバイダーを含み、音楽カタログは別途必要。開発時点のリプレイハーネスで SABR の応答処理に差が報告されている。 [クレートとライセンス][modern-manifest] · [再生][modern-player] · [リプレイ Issue][modern-issue] |
| InnerTubeX | GPL v3。Kotlin Multiplatform、Ktor、coroutines、QuackJS を採用し、デスクトップでは JVM を使う。SABR の入力、認証変更時のキャンセル、再生候補の選定をモジュール化している。Metrolist が採用しており、Rust へ部分移植する場合の設計資料にできる。 [ライセンス][itx-license] · [依存関係][itx-build] · [再生と認証状態][itx-source] |
| Metrolist | GPL v3。InnerTubeX を使い、Compose Multiplatform で音楽サービスを提供する。再生条件の変更による回帰が報告される一方、実プレーヤーのシークを計測するコードがある。統合先の UI としては Qt との差が大きく、再生計測とエラー分類の参考にする。 [ライセンス][metro-license] · [依存関係][metro-build] · [再生 Issue][metro-issue] · [ベンチマーク][metro-bench] |
| YouTube.js | MIT。Music のホーム、検索、ライブラリ、アーティスト、アルバム、プレイリストとプレーヤーのフォーマット選択を提供する。直接依存には JavaScript ランタイムが必要。既存のパーサー型や Music レスポンスの扱いを比較し、必要な部分を移植できる。 [依存関係とライセンス][js-manifest] · [Music API][js-music] · [フォーマット選択][js-player] |
| NewPipeExtractor | GPL v3。Java、Rhino、Protobuf を使用し、Music 検索と YouTube の音声抽出を実装する。NewPipe と MusicBeatSP が利用。JVM を追加する直接依存より、音声フォーマットとエラー分類のソースを参照するほうが既存エンジンに適合する。 [ライセンス][np-license] · [依存関係][np-build] · [ストリーム抽出][np-player] |
| MusicBeatSP | GPL v3。Android では Media3 を使い、InnerTubeX 0.7.0 と NewPipeExtractor 0.26.3 を再生リゾルバーに結合する。後続版のプレーヤースクリプトの解析不具合が依存固定の理由として記録されている。Android のセッション管理、WebView と JVM を含むため、失敗の抑制やキャッシュ失効などを部分的に参考にする。 [ライセンス][mb-license] · [Android 依存関係][mb-build] · [リゾルバー][mb-player] · [デスクトップ依存関係][mb-desktop] |
| ModernTube アプリ | MIT と BSD-3-Clause。前述の ModernTube のクレートを使う GTK4 アプリで、GStreamer と WebKitGTK を用いる。セグメントをシーク可能な入力として渡す実装があり、アプリ全体の直接採用には音声エンジンと認証方式の変更が伴う。 [依存関係とライセンス][modern-app-manifest] · [再生入力とシーク][modern-app-seek] |

2026年11月28日の GitHub 上では YouTube.js が約5,400スター、NewPipeExtractor が約2,100、Metrolist が約15,000、ytm-rs が約1,400、youtui が約210、Refryt が約60。NewPipeExtractor の v0.27.1 は11月10日、InnerTubeX の v0.13.2 は11月27日、Refryt の v0.3.3 は11月22日に公開された。スターは利用規模の目安であり、同じ条件で再生できるかは実測で判断する。 [YouTube.js][js-project] · [NewPipeExtractor][np-project] · [Metrolist][metro-project] · [ytm-rs][ytmrs-project] · [youtui][ytmapi-project] · [Refryt][refryt-project] · [InnerTubeX リリース][itx-release]

### 統合方式

| 方式 | 利点と負担 |
| --- | --- |
| 一つのライブラリへ直接依存 | 追従先を狭められる。調査した候補の一体型再生には、追加ランタイム、固有の音声エンジン、制限時のクライアント切替、資格情報の保存設計など Swiftify 側で変更すべき部分がある。 |
| フォークまたは部分移植 | 必要なパーサーや範囲入力を既存アーキテクチャに合わせて取り込める。元コミット、差分とテストフィクスチャを記録して、非公開 API の変更を追う。 |
| カタログライブラリと小さな再生入力の組み合わせ | ytmapi-rs のような既存型を活用し、ホームとロケールを補完する。セッション、HTTP クライアントとエラー分類を共有し、再生入力から既存エンジンへ直結する案を、実測に進める最初の候補とする。 |
| 独自実装 | 依存関係と利用する動作を最小化できる。全画面向けのカタログ・ページング・データ変換を自前で維持するコストが大きい。既存の保守対象から再利用できない箇所に絞る。 |

MIT の再利用には著作権表示と許諾文を残す。GPL の部分移植は元の対象ファイルと配布条件を確認し、Swiftify の GPL-3.0-or-later と両立する条件で出典・著作権表示・変更の記録を維持する。再利用した場合は同梱するコードや依存のライセンスも確認する。結合方式の確定は、実再生の実証結果と選定理由を提示して承認を得た後に行う。

## 認証の評価

- 一時的にシステムブラウザを開いて本人がログインし、明示的に同意した範囲の YouTube セッションをアプリへ引き渡す案を評価する。ytmapi-rs は Cookie を受け取り、Sonora は同じセッションでの更新を扱う。前者のトークン型は Cookie をシリアライズできるため、アプリ側で出力先の制御が必要。Premium の権限が実際に付与されることと、安全な引き渡し経路は未検証。現段階でブラウザ拡張などの追加配布物は選定していない。 [ytmapi-rs の Web セッション型][ytmapi-auth] · [Sonora のセッション管理][sonora-auth]
- 保管は Spotify の有効・無効にかかわらず OS の資格情報保管庫を使う。保管庫が使えない場合は、永続化せず一時セッションのみとする案を優先する。ログ、設定、キャッシュ、コマンドライン、環境変数、エラー報告から Cookie、認証ヘッダーと署名付き再生 URL を除外する。メディアや画像取得の HTTP クライアントへアカウント資格情報を渡さず、認証済み通信は対象とする HTTPS オリジンへ限定する。
- サーバーが返すセッション更新を、現在のセッションに属することを確認して反映する。失効、明示的な拒否、レート制限時は関連する要求をキャンセルし、再認証またはユーザーの再操作を待つ。ログアウト時は世代番号で遅延応答を無効にし、保管庫、メモリ、Cookie とそのセッションに紐づく再生情報を削除する。ログアウト後のネットワーク停止と資格情報が残らないことを合成データで確認する。
- サービスの検索、ブラウズ、プレーヤー、エラー識別の変更はサービスアダプター内部で吸収する。ユーザーの実アカウントは今回使用していない。Premium とアカウント機能の検証には本人が明示的に許可したテストセッションを使用する。

## 実ストリームの確認結果

2026年11月28日、クラウドから匿名で1回ずつ計測した。テスト音源は Kevin MacLeod の *Monkeys Spinning Monkeys*（CC BY 4.0）。作者が案内する公開動画は `2eZVbrO6Z1M`、同じ曲名・作者で Music の検索から得た ID は `NPdgPZ0u3zQ`。 [作者が案内する動画][sample-video] · [音源とライセンス][sample-license]

| 段階 | 観測結果 |
| --- | --- |
| `music.youtube.com` の作品ページ、`www.youtube.com`、`googlevideo.com` の HTTPS 接続 | クラウドのプロキシが CONNECT を HTTP 403 で拒否した。TLS 接続前で、オリジンの応答や音声データは取得していない。 |
| `youtubei.googleapis.com` の InnerTube 検索 | Music Web のコンテキスト `WEB_REMIX` を使い、Cookie、トークン、API キーなしで呼び出し。HTTP 200、606 ms、JSON 161,769 B。 |
| InnerTube プレーヤー `2eZVbrO6Z1M` | HTTP 200、306 ms、JSON 5,788 B。`UNPLAYABLE` / `The page needs to be reloaded.`。音声フォーマットとマニフェストは0件。 |
| InnerTube プレーヤー `NPdgPZ0u3zQ` | HTTP 200、316 ms、JSON 5,342 B。同じ `UNPLAYABLE` 応答。音声フォーマットとマニフェストは0件。 |

Music の作品ページに接続できず、暦日ベースのクライアントバージョン `1.20261128.01.00` を使った。JSON のバイト数は応答本文、時間は各リクエストの経過時間。曲ごとの権限や、開通した環境での再現性は未確定。同種の再読込応答は他クライアントからも報告されている。 [Metrolist の再生 Issue][metrolist-reload] · [Playlet の再生 Issue][playlet-reload]

GitHub Actions の匿名診断は、作品ページが返すクライアント設定を使い、サービスから直接再生可能な音声 URL が返れば最初の byte range を検証する。ローカルではリポジトリのルートで `python3 tools/probe-youtube-music.py` を実行できる。この診断はメディア入力の取得までをカバーし、既存エンジンでのデコードや出力はその次の検証になる。 [診断スクリプト](../tools/probe-youtube-music.py) · [GitHub Actions の定義](../.github/workflows/youtube-music-probe.yml)

サービスから音声データが到達しなかったため、ネイティブ再生は未実証で、シークや同期の性能値もない。次の検証は通常の HTTPS 接続が可能な環境で、同じ Music セッションが許可したフォーマットで部分取得とデコードを確認する。明示的な拒否に遭遇した時点でそのリクエスト経路を止める。

## 既存エンジンへの接続と測定

Qt 本体が使う Rust の `spotify-core` は Spotify 無効時にもローカル再生を担当する。`LocalTrackSource` は既存 DSP、EQ、テンポ・ピッチ処理を通し、出力 PCM の各フレームにメディア位置を付ける。`native-audio` の単体プレーヤーには `MediaProvider` とキャンセル可能なシーク入力がある。Qt 本体で既存機能を維持するには、同じ `MediaProvider` の契約を本体の `LocalTrackSource` へ取り込み、既存の DSP と出力時計に接続する。 [本体の再生処理](../spotify-core/src/file_playback.rs) · [入力の契約](../native-audio/src/media.rs)

リモート入力は同じメディアを指す新しい読み取り口を開けるようにする。取得範囲、総長、形式、応答元を照合し、キャッシュ量、同時要求数と待ち時間に上限を設ける。読取りの拒否・失敗は EOF と区別し、出力停止やシークで不要になった入力をキャンセルする。シークは別のデコーダーで準備してから切り替える。サービス拒否時に署名更新や別プロファイルへの切替でリトライする経路は設けない。ローカルファイルのシークと音声出力の復旧は既存の動作と比較する。

| 比較対象 | 条件と測定方法 |
| --- | --- |
| CPU、RSS・PSS、起動 | 同じリリース構成の Spotify 無効ビルド、同じ CachyOS、ハードウェア、Wayland / Hyprland / PipeWire で、起動完了、アイドル、同じ曲の連続再生を測る。CPU 時間と `/proc/<pid>/smaps_rollup` を採り、補助プロセスがあれば集計する。 |
| 検索、再生開始、シーク | 同じ曲 ID、検索語、クライアント状態、地域、音質と通信条件で、コールド・ウォームを分けて反復する。検索完了、最初の PCM 出力、指定位置の PCM 出力までを単調時計で測る。回数、中央値、p95、失敗件数を残す。 |
| 音質、バッファリング、通信量、音切れ | 同じフォーマットと出力レートでコーデック、ビットレート、サンプルレート、チャンネル数と再エンコードの有無を確認する。メディア・制御要求の転送量、バッファ最大値、枯渇回数、途切れとその時間を記録する。音質の比較では同じ録音を使い、DSP 設定を固定する。 |
| 再生時計、歌詞、テンポ変更 | 許可された同期基準付き音源と既存の LRC、Enhanced LRC、TTML を使用する。PipeWire モニターまたは実機ループバックで出力とタイムスタンプを照合する。開始、停止・再開、シーク、バッファ枯渇、出力復旧、1倍速と変速時の位置誤差、行・単語キュー、曲別オフセットを比較する。仮想デバイスでの出力確認と実機の可聴遅延は別に記録する。 |

[ytmapi-manifest]: https://github.com/nick42d/youtui/blob/d381ea4ce9ec4eb667c7bf8b484bc2db94931c76/ytmapi-rs/Cargo.toml
[ytmapi-queries]: https://github.com/nick42d/youtui/blob/d381ea4ce9ec4eb667c7bf8b484bc2db94931c76/ytmapi-rs/src/simplified_queries.rs
[ytmapi-http]: https://github.com/nick42d/youtui/blob/d381ea4ce9ec4eb667c7bf8b484bc2db94931c76/ytmapi-rs/src/client.rs
[ytmapi-auth]: https://github.com/nick42d/youtui/blob/d381ea4ce9ec4eb667c7bf8b484bc2db94931c76/ytmapi-rs/src/auth/browser.rs
[ytmapi-issue]: https://github.com/nick42d/youtui/issues/396
[rp-manifest]: https://codeberg.org/ThetaDev/rustypipe/src/commit/c93bafd8416843fc68251f9ce8830a89504c185e/Cargo.toml
[rp-player]: https://codeberg.org/ThetaDev/rustypipe/src/commit/c93bafd8416843fc68251f9ce8830a89504c185e/src/client/player.rs
[rp-storage]: https://codeberg.org/ThetaDev/rustypipe/src/commit/c93bafd8416843fc68251f9ce8830a89504c185e/src/client/mod.rs
[rp-release]: https://codeberg.org/ThetaDev/rustypipe/releases/tag/rustypipe/v0.11.4
[caya-manifest]: https://github.com/caya8205-2/innertube-rs/blob/8c212dbbb6b7eabed09d1201ede48fc2010e5a55/Cargo.toml
[caya-music]: https://github.com/caya8205-2/innertube-rs/blob/8c212dbbb6b7eabed09d1201ede48fc2010e5a55/src/endpoints/music.rs
[caya-player]: https://github.com/caya8205-2/innertube-rs/blob/8c212dbbb6b7eabed09d1201ede48fc2010e5a55/src/endpoints/player.rs
[caya-update]: https://github.com/caya8205-2/innertube-rs/pull/20
[sonora-manifest]: https://github.com/sonorahq/ytmusic-rs/blob/22ee5d4ec1c4d7ad03760c05351bc039c57934a2/Cargo.toml
[sonora-stream]: https://github.com/sonorahq/ytmusic-rs/blob/22ee5d4ec1c4d7ad03760c05351bc039c57934a2/src/stream.rs
[sonora-player]: https://github.com/sonorahq/ytmusic-rs/blob/22ee5d4ec1c4d7ad03760c05351bc039c57934a2/src/player.rs
[sonora-auth]: https://github.com/sonorahq/ytmusic-rs/blob/22ee5d4ec1c4d7ad03760c05351bc039c57934a2/src/client.rs
[sonora-adoption]: https://github.com/sonorahq/sonora/issues/836
[sonora-update]: https://github.com/sonorahq/ytmusic-rs/pull/21
[ytmrs-manifest]: https://github.com/grapedjuice/ytm-rs/blob/45a75e08dc9f24e001d59f5910497ddc58b01db6/Cargo.toml
[ytmrs-player]: https://github.com/grapedjuice/ytm-rs/blob/45a75e08dc9f24e001d59f5910497ddc58b01db6/src/innertube.rs
[ytmrs-login]: https://github.com/grapedjuice/ytm-rs/blob/45a75e08dc9f24e001d59f5910497ddc58b01db6/src/login.rs
[ytmrs-vault]: https://github.com/grapedjuice/ytm-rs/blob/45a75e08dc9f24e001d59f5910497ddc58b01db6/src/vault.rs
[ytmrs-issue]: https://github.com/grapedjuice/ytm-rs/issues/457
[refryt-manifest]: https://github.com/gridoe/refryt/blob/f933c4fb117bb9219e29a35327e336f4def9098c/Cargo.toml#L5-L12
[refryt-chain]: https://github.com/gridoe/refryt/blob/f933c4fb117bb9219e29a35327e336f4def9098c/crates/refryt-resolve/src/chain.rs
[refryt-auth]: https://github.com/gridoe/refryt/blob/f933c4fb117bb9219e29a35327e336f4def9098c/crates/refryt/src/auth.rs
[refryt-issue]: https://github.com/gridoe/refryt/issues/36
[modern-manifest]: https://github.com/moderntube/innertube-rs/blob/40a419174d39b78838e739107b0a29d12bf1b494/Cargo.toml
[modern-player]: https://github.com/moderntube/innertube-rs/blob/40a419174d39b78838e739107b0a29d12bf1b494/crates/innertube/src/playback.rs
[modern-issue]: https://github.com/moderntube/innertube-rs/issues/2
[itx-license]: https://github.com/MetrolistGroup/innertubex/blob/d5d711503af55c85b52081f2978f9cfaa1466077/LICENSE
[itx-build]: https://github.com/MetrolistGroup/innertubex/blob/d5d711503af55c85b52081f2978f9cfaa1466077/build.gradle.kts#L27-L64
[itx-source]: https://github.com/MetrolistGroup/innertubex/blob/d5d711503af55c85b52081f2978f9cfaa1466077/src/commonMain/kotlin/com/metrolist/innertubex/InnerTube.kt#L323-L416
[itx-release]: https://github.com/MetrolistGroup/innertubex/releases/tag/v0.13.2
[metro-license]: https://github.com/MetrolistGroup/Metrolist/blob/dd8d66ae9f426c00296b90c7413fb2a6e51b8fb2/LICENSE
[metro-build]: https://github.com/MetrolistGroup/Metrolist/blob/dd8d66ae9f426c00296b90c7413fb2a6e51b8fb2/shared/build.gradle.kts#L323-L340
[metro-issue]: https://github.com/MetrolistGroup/Metrolist/issues/4656
[metro-bench]: https://github.com/MetrolistGroup/Metrolist/blob/dd8d66ae9f426c00296b90c7413fb2a6e51b8fb2/shared/src/jvmSharedMain/kotlin/com/metrolist/shared/youtube/benchmark/PlaybackBenchmarkRunner.kt#L239-L269
[js-manifest]: https://github.com/LuanRT/YouTube.js/blob/77703faa19e0214735be8db905e262c9e4a43a63/package.json
[js-music]: https://github.com/LuanRT/YouTube.js/blob/77703faa19e0214735be8db905e262c9e4a43a63/src/core/clients/Music.ts#L135-L220
[js-player]: https://github.com/LuanRT/YouTube.js/blob/77703faa19e0214735be8db905e262c9e4a43a63/src/Innertube.ts#L449-L474
[np-license]: https://github.com/TeamNewPipe/NewPipeExtractor/blob/ebc0b8c8b1e08300d54ce6413720b33695cd6075/LICENSE
[np-build]: https://github.com/TeamNewPipe/NewPipeExtractor/blob/ebc0b8c8b1e08300d54ce6413720b33695cd6075/extractor/build.gradle.kts#L81-L101
[np-player]: https://github.com/TeamNewPipe/NewPipeExtractor/blob/ebc0b8c8b1e08300d54ce6413720b33695cd6075/extractor/src/main/java/org/schabi/newpipe/extractor/services/youtube/extractors/YoutubeStreamExtractor.java#L870-L920
[mb-license]: https://github.com/Kdroidwin/MusicBeatSP/blob/bb19e219fd43b0eb9054b731f4357587db258c55/LICENSE
[mb-build]: https://github.com/Kdroidwin/MusicBeatSP/blob/bb19e219fd43b0eb9054b731f4357587db258c55/app/build.gradle.kts#L379-L389
[mb-player]: https://github.com/Kdroidwin/MusicBeatSP/blob/bb19e219fd43b0eb9054b731f4357587db258c55/app/src/main/java/com/music/bitchord/data/innertube/StreamResolver.kt
[mb-desktop]: https://github.com/Kdroidwin/MusicBeatSP/blob/bb19e219fd43b0eb9054b731f4357587db258c55/linux-offline/shared/build.gradle.kts#L25-L40
[modern-app-manifest]: https://github.com/moderntube/moderntube/blob/bb7099b29ce1cb6dc3e6f0daec2bca9ade3a42cf/Cargo.toml
[modern-app-seek]: https://github.com/moderntube/moderntube/blob/bb7099b29ce1cb6dc3e6f0daec2bca9ade3a42cf/src/playback/gst/substrate/feed.rs#L18-L72
[js-project]: https://github.com/LuanRT/YouTube.js
[np-project]: https://github.com/TeamNewPipe/NewPipeExtractor
[metro-project]: https://github.com/MetrolistGroup/Metrolist
[ytmrs-project]: https://github.com/grapedjuice/ytm-rs
[ytmapi-project]: https://github.com/nick42d/youtui
[refryt-project]: https://github.com/gridoe/refryt
[sample-video]: https://incompetech.com/wordpress/2014/02/monkeys-spinning-monkeys/
[sample-license]: https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1400011
[metrolist-reload]: https://github.com/MetrolistGroup/Metrolist/issues/4864
[playlet-reload]: https://github.com/iBicha/playlet/issues/834
