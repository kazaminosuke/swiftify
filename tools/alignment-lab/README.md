# Alignment Lab

Qt Quick の独立アプリと共通 CLI で、ローカル音源と手元の歌詞を比較できます。
Swiftify のルート CMake、再生プロセス、モデル配布とは独立した実行ターゲットです。

## CachyOS / Linux で起動

Qt 6.8 以降の Qt Quick / Qt Quick Controls、CMake、C++20 コンパイラー、
Python 3.10 以降、FFmpeg が必要です。Swiftify リポジトリのルートで実行します。

```sh
cmake -S tools/alignment-lab -B build/alignment-lab -G Ninja
cmake --build build/alignment-lab
./build/alignment-lab/alignment-lab
```

Qt の独自インストール先を使う場合は `-DCMAKE_PREFIX_PATH=/path/to/Qt` を指定します。
Wayland では Qt が選んだプラグインで起動します。`QT_QPA_PLATFORM=wayland` で明示できます。
Swiftify を通常どおりビルドするだけなら本ツールのセットアップは不要です。取り外すときは
`tools/alignment-lab/` と独立ビルドディレクトリを外します。

モデルを使わずに Heuristic Sync を試せます。ONNX モデルでは別環境に
`numpy` と `onnxruntime` を入れ、日本語では `pykakasi` を追加します。

```sh
python3 -m venv tools/alignment-lab/.venv
tools/alignment-lab/.venv/bin/pip install 'numpy>=1.24,<3' 'onnxruntime>=1.20,<2' 'pykakasi>=2.3,<3'
ALIGNMENT_LAB_PYTHON="$PWD/tools/alignment-lab/.venv/bin/python" ./build/alignment-lab/alignment-lab
```

「Models」でプリセットに ONNX を指定するか、互換エクスポートを名前・ライセンス・出典付きで
追加します。削除は登録を解除します。ライセンスが異なるエクスポートには新しいIDを付けます。
CPU、GPU、Auto から選べます。現行 GPU 経路は FP32 の ONNX Runtime CUDA です。
利用する場合は CPU 版とは別の Python 環境に `onnxruntime-gpu` と対応する CUDA / cuDNN を
用意します。プロセス VRAM の計測には NVIDIA NVML とオプションの `nvidia-ml-py` が必要です。

ローカル音源と UTF-8 の歌詞を指定して Align を実行します。チェックしたモデルは同じ入力で
Benchmark できます。結果選択欄で各モデルの行・単語・モーラと冷間・温間の計測値を
確認できます。LRC と Enhanced LRC は10 ms単位、JSON はミリ秒で、
日本語モーラは JSON に階層として入ります。日本語 CTC の読みは機械変換なので、特殊な読みは
ひらがなで入力できます。日本語 HuBERT は日本語のかな読みを対象にします。

## CLI

以下は `tools/alignment-lab/` から実行します。GUI と同じアダプターと登録を利用します。

```sh
python3 -m alignment_lab models
python3 -m alignment_lab register --id mms-q8 --profile mms-q8 --file /path/to/model.q8.onnx
python3 -m alignment_lab register --id hubert-ja-q4 --profile hubert-ja-q4 --file /path/to/model_q4.onnx
python3 -m alignment_lab align --model heuristic --device cpu --language ja \
  --audio /path/to/track.flac --lyrics /path/to/lyrics.txt --format enhanced_lrc --output /path/to/track.lrc
python3 -m alignment_lab benchmark --model mms-q8 --device cpu --threads 2 --warm 2 --language ja \
  --audio /path/to/track.flac --lyrics /path/to/lyrics.txt --reference /path/to/reference.json
python3 -m alignment_lab suite /path/to/manifest.json --device cpu --threads 2 --warm 2 \
  --model heuristic --model mms-q8 --model hubert-ja-q4
python3 -m alignment_lab remove --id mms-q8
```

参照 JSON の `lines`、`words`、`syllables` に、`start_ms` と任意の `end_ms` を指定します。
キーは0始まりの `line`、単語の場合は追加で `word`、モーラの場合はさらに `syllable` です。
任意の `text` を入れると同じ表記だけ評価します。例は
`{"lines":[{"line":0,"start_ms":1265,"end_ms":5680}]}` です。マニフェストでは
`audio`、`lyrics`、`language`、`kind`、必要に応じて `reference` を指定します。
パスの基準はマニフェストの親ディレクトリです。

CLI の `align --cache --device cpu` では入力ファイルとモデルの識別情報、歌詞、バージョン、
デバイス、スレッド数をキーとして結果を保存します。計測ではキャッシュを使いません。
登録先は `${XDG_CONFIG_HOME:-~/.config}/swiftify-alignment-lab/models.json`、
キャッシュ先は `${XDG_CACHE_HOME:-~/.cache}/swiftify-alignment-lab/` です。
評価用の分離は `ALIGNMENT_LAB_REGISTRY=/path/to/models.json` で指定できます。

CLI は音声前処理、正規化、モデルアダプター、整列、エクスポート、計測をライブラリーとして利用します。
Qt はローカルワーカーを非同期起動し、JSON Lines で進捗と結果を受け取ります。Linux のキャンセルは
FFmpeg を含むワーカープロセスグループを終了します。音源は最大15分、歌詞は最大64 KiBです。

```sh
python3 -B -m unittest discover -s tests -v
ctest --test-dir ../../build/alignment-lab --output-on-failure
```

[モデル調査・CPU実測・データの再現手順](MODEL-EVALUATION.md)
