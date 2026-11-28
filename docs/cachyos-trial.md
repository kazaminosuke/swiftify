# CachyOS で Swiftify を試す

フォークの開発と実機検証には `integration`、上流追従には `main` を使います。
試用対象は x86_64 の CachyOS、Hyprland、Wayland、PipeWire です。

## ダウンロードと起動

GitHub の Actions で `integration` に絞り込み、試したいコミットの
`prebuilt` が成功したことを確認します。Artifacts から `linux-disabled` を取得して
ZIP を展開します。アーカイブは Qt を同梱した Spotify 無効構成です。
`.dependencies.txt` にはビルド元コミット、Cargo の有効な依存グラフ、
同梱ライブラリとホスト側のライブラリが記録されています。
[パッケージ生成ワークフロー](../.github/workflows/prebuilt.yml)

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
Wayland セッション内の Swiftify を検出します。アプリの設定、試用中に作成したプレイリスト、キャッシュは上の `profile` に保存されます。
Wayland と PipeWire には現在のセッションを使います。

日本語を表示するには、設定の表示言語から日本語を選びます。日本語フォントが
入っていない場合は、CachyOS の `noto-fonts-cjk` を使用できます。

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

試用版では、ローカル再生と音声処理を上記の手順で確認できます。
YouTube Music は [バックエンド検証](youtube-music-backend.md)を参照してください。
[Alignment Lab](../tools/alignment-lab/README.md) は別途ビルドする任意ツールです。

## CachyOS でソースからビルドする場合

ディストリビューションで Qt 6.12.1 以上と下記の開発パッケージを揃えます。
既に揃っている場合はユーザー権限で構成、ビルド、インストールできます。
フォークのチェックアウトのルートで実行します。

```bash
sudo pacman -S --needed base-devel git cmake ninja rust pkgconf qcoro \
  qt6-base qt6-declarative qt6-shadertools qt6-wayland libpulse alsa-lib

git switch integration
cmake -S . -B build-cachyos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SPOTIFY=OFF -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/swiftify-cachyos"
cmake --build build-cachyos
cmake --install build-cachyos
```

上と同じプロファイル分離を使い、実行ファイルのパスを
`$HOME/.local/opt/swiftify-cachyos/bin/swiftify` に置き換えて起動します。
