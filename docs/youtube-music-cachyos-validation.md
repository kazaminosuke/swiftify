# CachyOS での YouTube Music バックエンド検証

調査時点で実ストリームを取得できていないため、ここでは音声経路の成立と候補のカタログ処理を別々に測る。候補コードは一時ディレクトリで動かし、Swiftify 本体のビルドと依存関係には影響しない。クラウドの結果と選定前の設計案は [調査記録](youtube-music-backend.md) を参照。

## 公式配信の音声入力

CachyOS 上の Swiftify チェックアウトから実行する。音源は作者が案内する Kevin MacLeod の CC BY 4.0 作品 *Monkeys Spinning Monkeys*（`2eZVbrO6Z1M`）。Python 3 の標準ライブラリだけを使う。 [音源とライセンス](https://incompetech.com/music/royalty-free/index.html?isrc=USUAN1400011) · [作者の案内](https://incompetech.com/wordpress/2014/02/monkeys-spinning-monkeys/)

```sh
python3 tools/probe-youtube-music.py
```

測定結果には初期応答、プレーヤー応答、配信先の応答時間と本文サイズが出る。直接音声を提供された場合に限り、先頭 4 KiB 以下の範囲応答とコンテナを確かめる。スクリプトはアカウント情報を読み込まず、音声 URL や Cookie を出力しない。サービスが拒否、同意、再認証、追加のアクセス確認を要求した場合は表示された段階で止める。部分取得が通れば、次回は実際の HTTP 入力を既存プレーヤーへつなぎ、デコード、PCM 出力、シークを証明する。

## 匿名カタログの比較

同じ CachyOS、ネットワーク、検索語、楽曲フィルター、匿名状態で候補を測る。ビルドとパッケージ取得を先に済ませ、候補を交互に複数回実行する。先頭ページの楽曲数と対象 ID の有無を記録する。Music 検索で観測した同曲のカタログ ID は `NPdgPZ0u3zQ`。ライブラリによってエンドポイント、ロケール、クライアントバージョンが異なる場合がある。その差と失敗を比較結果に含める。

```sh
query='Kevin MacLeod Monkeys Spinning Monkeys'
target='NPdgPZ0u3zQ'
probe_dir=$(mktemp -d)
mkdir -p "$probe_dir/rust/src" "$probe_dir/js"
```

### ytmapi-rs

`$probe_dir/rust/Cargo.toml`

```toml
[package]
name = "swiftify-catalogue-probe"
version = "0.1.0"
edition = "2024"

[dependencies]
reqwest = { version = "0.13.3", default-features = false, features = ["rustls"] }
tokio = { version = "1", features = ["macros", "rt"] }
ytmapi-rs = { git = "https://github.com/nick42d/youtui", rev = "d381ea4ce9ec4eb667c7bf8b484bc2db94931c76", default-features = false, features = ["reqwest", "rustls", "simplified-queries"] }
```

`$probe_dir/rust/src/main.rs`

```rust
use std::{env, error::Error, time::Duration};
use ytmapi_rs::{Client, YtMusicBuilder, common::YoutubeID};

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), Box<dyn Error>> {
    let query = env::args().nth(1).expect("search query");
    let target = env::args().nth(2).expect("target video ID");
    let http = reqwest::Client::builder()
        .redirect(reqwest::redirect::Policy::none())
        .timeout(Duration::from_secs(8))
        .build()?;
    let music = YtMusicBuilder::new_with_client(Client::new_from_reqwest_client(http))
        .build()
        .await?;
    let rows = music.search_songs(query.as_str()).await?;
    let found = rows.iter().any(|song| song.video_id.get_raw() == target.as_str());
    println!("songs={} target_found={found}", rows.len());
    Ok(())
}
```

```sh
cargo build --release --manifest-path "$probe_dir/rust/Cargo.toml"
/usr/bin/time -f 'elapsed=%es user=%Us sys=%Ss peak_rss=%MkB' \
  timeout 15s "$probe_dir/rust/target/release/swiftify-catalogue-probe" "$query" "$target"
```

匿名クライアントを使う。ライブラリに同梱された Web クライアントの公開キーを使う場合があり、ユーザー自身の API キーやアカウントの入力は不要。 [固定リビジョンのクエリー](https://github.com/nick42d/youtui/blob/d381ea4ce9ec4eb667c7bf8b484bc2db94931c76/ytmapi-rs/src/simplified_queries.rs)

### YouTube.js

Node.js 24 と npm を使う。`$probe_dir/js/search.mjs`

```js
import { Innertube, YTNodes } from 'youtubei.js';

const [query, target] = process.argv.slice(2);
const music = await Innertube.create({
  generate_session_locally: true,
  retrieve_player: false,
  retrieve_innertube_config: false,
  enable_session_cache: false,
  fetch: (input, init) => fetch(input, {
    ...init, redirect: 'error', credentials: 'omit', signal: AbortSignal.timeout(8000)
  }),
});
const result = await music.music.search(query, { type: 'song' });
const rows = result.page.contents_memo?.getType(YTNodes.MusicResponsiveListItem) ?? [];
const songs = rows.filter(row => row.item_type === 'song' && /^[A-Za-z0-9_-]{11}$/.test(row.id ?? ''));
console.log(`songs=${songs.length} target_found=${songs.some(row => row.id === target)}`);
```

```sh
(
  cd "$probe_dir/js"
  npm install --ignore-scripts --no-audit --no-fund --save-exact youtubei.js@18.3.0
  /usr/bin/time -f 'elapsed=%es user=%Us sys=%Ss peak_rss=%MkB' \
    timeout 15s node --use-system-ca --use-env-proxy search.mjs "$query" "$target"
)
```

このコードはクライアントのセッションをローカルに組み立て、プレーヤースクリプト、アカウントや追加ページの取得を行わない。CPU と最大 RSS は各 CLI プロセス、経過時間は起動を含む。接続エラーも記録し、比較結果を Swiftify 本体や実再生の性能へ外挿しない。 [YouTube.js v18.3.0](https://github.com/LuanRT/YouTube.js/releases/tag/v18.3.0)

## 次回の判定

1. 許可された応答から直接音声の範囲取得を確認する。拒否や途中のエラーは段階とともに記録する。
2. 同じ配信を既存 `spotify-core` の出力に接続し、先頭 PCM、再生、シーク、キャンセル、回線切断時の停止を確認する。
3. PipeWire の出力と既存エンジンの再生時計を比較し、EQ、テンポ・ピッチ、LRC・Enhanced LRC・TTML、曲別オフセットを維持できたことを確認する。
4. 音声経路、候補の実測、認証と依存関係の制約を提示して採用方式の承認を得る。
