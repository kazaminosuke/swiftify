# CachyOS で Swiftify を試す

フォークの開発と実機検証には `integration`、上流追従には `main` を使います。
試用対象は x86_64 の CachyOS、Hyprland、Wayland、PipeWire です。

## ダウンロードと起動

GitHub にログインし、`integration` の成功した実行から `linux-disabled` を取得して
ZIP を展開します。アーカイブは Qt 6.12.1 を同梱した Spotify 無効構成です。
`.dependencies.txt` にはビルド元コミット、Cargo の有効な依存グラフ、
同梱ライブラリとホスト側のライブラリが記録されています。
[GitHub Actions](https://github.com/kazaminosuke/swiftify/actions/workflows/prebuilt.yml?query=branch%3Aintegration)

展開したアーカイブとチェックサムがあるディレクトリで実行します。
パッケージ名はダウンロードしたバージョンに合わせてください。

```bash
package=swiftify-0.26.2-linux-x86_64-local
trial="$HOME/.local/opt/swiftify-trial"
sha256sum -c "$package.tar.gz.sha256" &&
  mkdir -p "$trial" &&
  tar -xzf "$package.tar.gz" -C "$trial" &&
  env QT_QPA_PLATFORM=wayland \
    XDG_CONFIG_HOME="$trial/profile/config" \
    XDG_DATA_HOME="$trial/profile/data" \
    XDG_CACHE_HOME="$trial/profile/cache" \
    XDG_STATE_HOME="$trial/profile/state" \
    "$trial/$package/bin/swiftify"
```

試用前に既存の Swiftify を終了してください。再生ウィンドウは同じ
Wayland セッション内の Swiftify を検出します。アプリの設定、試用中に作った
プレイリスト、キャッシュは上の `profile` に入ります。
Wayland と PipeWire には現在のセッションを使います。

音が出ないときは `wpctl status` で PipeWire の既定出力を確認します。
CachyOS での通常の音声経路は `pipewire-alsa`、`pipewire-pulse` と
WirePlumber です。ローダーのエラーは `ldd "$trial/$package/bin/swiftify"`、
Wayland プラグインの問題は上の起動コマンドに `QT_DEBUG_PLUGINS=1` を追加すると
確認できます。

削除するときはアプリを終了し、`$HOME/.local/opt/swiftify-trial` と取得した
ZIP を削除します。試用中に編集した音楽ファイルや同じ場所に保存した歌詞は
そのまま残ります。元ファイルを保ちたい場合は音源のコピーで試してください。

## 実機で確認する項目

- 新規プロファイルの起動、Hyprland のウィンドウ表示とスケーリング、フォント、ファイルピッカー
- ローカル音源の追加、再生、曲送り、EQ、テンポ・ピッチ、シーク後の一時停止と再開
- 44.1 kHz と 48 kHz の再生、USB／Bluetooth の抜き差しと PipeWire での出力復旧、MPRIS の位置とメディアキー
- 音源と同名の `.lrc`／`.ttml` の表示、シーク時の行・単語の追従、曲別オフセットの保存
- 未設定のローカル起動と再生中の外部通信

通常起動はローカルライブラリです。`--youtube` は別ウィンドウのカタログを
開きます。カタログのデータ取得には Google 側の設定が必要です。
YouTube Music のアプリ内ストリーミングと既存の再生キューへの接続は未実装です。
Alignment Lab は独立したツールです。
[YouTube カタログの仕様](youtube-catalogue.md) · [Alignment Lab の Draft](https://github.com/kazaminosuke/swiftify/pull/15)

## CachyOS でソースからビルドする場合

ディストリビューションで Qt 6.12.1 以上と下記の開発パッケージを揃えます。
既に揃っている場合はユーザー権限で構成、ビルド、インストールできます。

```bash
sudo pacman -S --needed base-devel git cmake ninja rust pkgconf qcoro \
  qt6-base qt6-declarative qt6-shadertools qt6-wayland qt6-networkauth libpulse alsa-lib

git clone --branch integration https://github.com/kazaminosuke/swiftify.git
cd swiftify
cmake -S . -B build-cachyos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SPOTIFY=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/swiftify-cachyos"
cmake --build build-cachyos
cmake --install build-cachyos
```

上と同じプロファイル分離を使い、実行ファイルのパスを
`$HOME/.local/opt/swiftify-cachyos/bin/swiftify` に置き換えて起動します。
