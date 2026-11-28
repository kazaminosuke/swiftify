# 歌詞強制アラインメントの評価

## 候補と条件

Swiftify と Alignment Lab のコードは GPL-3.0-or-later です。
各重み、学習データ、評価音源にはそれぞれの条件が適用されます。
Lab の配布物に重みと評価音源は含まれません。モデルの入手は配布元からの手動取得です。

| 候補 | 粒度、日本語と伴奏 | 容量・実行条件 | ライセンスと導入 |
| --- | --- | --- | --- |
| MusicBeatSP の MMS-300M-1130 ONNX FP32 | ラテン文字 CTC から行・単語・日本語モーラへ集約。伴奏付きとボーカル分離後を比較できる。MusicBeatSP Android の CTC 選択肢と同じ配布元。 | 1,262,408,964 B。ONNX Runtime と NumPy、日本語は pykakasi。CPU実測、CUDA未検証。 | Meta MMS、MahmoudAshraf のパッケージ、romara-labs の変換を継承した重みは CC BY-NC 4.0。非商用、出典、改変の明示等が条件。アプリとの同梱は別判断。[重みとモデルカード][mms] ・ [MusicBeatSP の参照][beat] |
| MMS-300M-1130 ONNX INT8 | FP32 と同じ語彙とフレーム。量子化だけの差を同じ入力で比較できる。 | 356,289,622 B。同じCPUランタイム。動的量子化済みの配布ファイル。 | FP32 と同じ CC BY-NC 4.0。[INT8の出典][mms] |
| Japanese HuBERT v4 ONNX INT4 | ReazonSpeech 由来の日本語音素 CTC。行・単語・かなモーラに集約。歌唱専用の学習や精度保証はモデルカードにない。 | 63,010,793 B。CPU ONNX Runtime。MatMul 4 bit、Conv 重み 8 bit。 | rinna、prj-beatrice、air-air の系譜。Apache-2.0。再配布にはライセンスと適用される告知を付ける。元重みと変換のリビジョンを固定。[ONNX版][hubert] ・ [元モデル][beatrice] |
| Qwen3-ForcedAligner-0.6B | 日本語を含む11言語、単語・文字等、音声は最大5分が公表仕様。ASR の中国語・英語歌唱 WER はこのアライナーの日本語歌唱誤差と別指標。 | safetensors 1,835,544,544 B ＋ tokenizer。Qwen ASR / Transformers / PyTorch。CPU、GPU、量子化とも今回未計測。 | コードと公開重みは Apache-2.0。配布時は Apache の告知条件を確認。調査候補。[配布元][qwen] ・ [技術報告][qwen-paper] |
| pyshiro / SHIRO | 日本語歌唱の音素向け HSMM。かなから音素を作り、休止・息をモデル化できる。伴奏条件の検証が必要。 | NumPy / SciPy / Numba / SoundFile / msgpack と学習済み HSMM。CPU 候補。容量・速度は未計測。 | pyshiro と元実装は GPL-3.0。サブモジュールの学習済み重みの利用条件は未確定。調査候補。[実装とクレジット][shiro] |
| SOFA / SingAlign | 歌唱向け音素境界。SOFA デモには中国語用辞書があり、日本語は音素辞書・重み・評価セットを要確認。SingAlign は多言語歌唱候補。 | PyTorch 系。個別チェックポイントのサイズと CPU/GPU 負荷は未計測。 | SOFA コードは MIT。SOFA の各重みと SingAlign の各配布条件は別に確認が必要。調査候補。[SOFA][sofa] ・ [SingAlign][singalign] |
| WhisperX / whisper.cpp | WhisperX は ASR と言語別 wav2vec2 を組み合わせ、日本語の既定は XLSR-53 の別モデル。whisper.cpp の DTW は ASR の出力トークンを時刻化する。既知歌詞と照合する工程が必要。 | WhisperX は PyTorch / faster-whisper 等。whisper.cpp はネイティブ CPU、各種 GPU、整数重みに対応。精度・容量はモデル別、今回未計測。 | コードは BSD-2-Clause / MIT。Whisper、XLSR、辞書等の条件は重みごとに確認。調査候補。[WhisperX既定モデル][whisperx] ・ [whisper.cpp][whispercpp] |
| RomajiASR / lyric-align / lyric_align | 日本語歌唱向け音素 ASR、CJK の歌詞照合、形態素読み＋音源分離＋CTC などの実装例。強制整列には発音と境界の検証が必要。 | 推論と分離に追加依存。配布される各重みと保守状態を要確認。 | RomajiASR は Apache-2.0、ijuinryukichi の実装は MIT。コードの条件はデータや歌唱素材の利用許諾を含まない。調査候補。[RomajiASR][romaji] ・ [CJK GUI/CLI候補][cjkalign] ・ [日本語読みの実装][lamptales] |
| Heuristic Sync | 行・単語・利用可能な日本語モーラに文字数で時間を割り付ける。音響的な境界の推定に使わない。伴奏・休止は時間に含まれる。 | 重み0 B。Python標準ライブラリーと FFmpeg。 | Lab の実装は GPL-3.0-or-later。端末にある FFmpeg をそのビルドの条件で使う。[FFmpeg の条件][ffmpeg] |

MusicBeatSP、MusicBeat、BitChord の歌詞処理は設計資料として参照しました。
Alignment Lab の処理は本リポジトリで実装しました。モデルの出典は上表の通りです。

## 2026-11-27 の CPU 計測

Debian 13 x86-64、Intel Xeon Platinum 8573C、CPU クォータ2、推論2スレッド。
Python 3.12.14、ONNX Runtime 1.32.0、NumPy 2.5.5、pykakasi 2.3.0、FFmpeg 7.1.5。
コールドは各音源とモデルのペアを新規プロセスで起動した初回、ウォームは同じセッションの
2回です。OSのページキャッシュは維持されます。総時間はロード、PCM・テキスト前処理、
推論・CTC、結果生成を含みます。プロセスの作成時間は計測の外です。
RAM は10 ms間隔でワーカーと FFmpeg の RSS を合算したピークです。
共有 VM の負荷で時間にばらつきがあります。GPU、VRAM は未検証です。

JSUT-song 070 は13.201秒の単独歌唱です。公開 HTS ラベルの音素を行・語・モーラに対応付けました。
伴奏条件は同じ歌唱に生成した静かな和音をミックスしたものです。
音源分離は両条件とも使っていません。各値は開始時刻の絶対誤差です。
全ケースの開始・終了の平均、中央値、P95、カバレッジ、各段階の時間と RTF は
[CPU 生データ](benchmarks/cpu-2026-11-27.json) にあります。

| 音源 | モデル | ロード ms | 総時間 冷間 / 温間平均 ms | ピーク RAM MiB | 行開始 平均 / P95 ms | 単語 / モーラ開始 平均 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 日本語歌唱 | Heuristic | <1 | 382 / 90 | 125 | 863 / 1265 | 1172 / 1253 |
| 日本語歌唱 | MMS FP32 | 6352 | 9199 / 3045 | 2464 | 32.5 / 35 | 75 / 60.6 |
| 日本語歌唱 | MMS INT8 | 1281 | 3506 / 1496 | 1201 | 32.5 / 35 | 75 / 61.3 |
| 日本語歌唱 | HuBERT INT4 | 316 | 1692 / 1157 | 662 | 42.5 / 70 | 77.2 / 63.5 |
| 日本語歌唱＋生成伴奏 | Heuristic | <1 | 307 / 98 | 118 | 863 / 1265 | 1172 / 1253 |
| 日本語歌唱＋生成伴奏 | MMS FP32 | 7558 | 15980 / 6906 | 2464 | 42.5 / 55 | 77.2 / 61.3 |
| 日本語歌唱＋生成伴奏 | MMS INT8 | 2565 | 8499 / 5533 | 1209 | 52.5 / 55 | 79.4 / 62.0 |
| 日本語歌唱＋生成伴奏 | HuBERT INT4 | 951 | 4950 / 3210 | 664 | 42.5 / 70 | 79.4 / 64.3 |

歌唱の各行は2行・9語・27モーラの参照です。
この音源では MMS の量子化で単独歌唱の行誤差は同じで、伴奏付きでは平均が10 ms増えました。
HuBERT は95%小さいファイルで、単独歌唱の行開始平均は FP32 より10 ms増えました。
伴奏条件の行平均は同値でした。歌手・楽曲・伴奏アレンジを増やした場合の優劣は未確定です。

一般音声は FLEURS validation の日本語 ID 1519（11.100秒）と英語 ID 1548（6.540秒）です。
公式の発話テキストを使用し、参照時刻がないため誤差は `null` です。
日本語での温間総時間は Heuristic 77 ms、MMS FP32 2550 ms、INT8 1110 ms、HuBERT 1412 ms。
英語ではそれぞれ58 ms、1978 ms、577 msでした。15ペア、各3試行が完了し、実行失敗は0件でした。
ラベルとの不一致件数は実行失敗率とは別の値です。正解のない音源でも CTC は強制的に時刻を
返せるため、処理完了は精度の証明にはなりません。

## 再現

1. 上表の固定リビジョンから各 ONNX ファイルを手動で入手して、Lab の登録機能で指定します。
   SHA-256 は下表にあります。
2. [JSUT-song 配布ページ][jsut] の音声 ZIP とラベル ZIP を手元に保存します。
   配布元は個人利用と非商用研究を許可しています。テストで作る元音源と伴奏ミックスは手元に保存します。
3. `tools/alignment-lab/` で以下を実行します。

```sh
python3 benchmarks/prepare_jsut.py /path/to/jsut-song_ver1.zip /path/to/jsut-song_label.zip /tmp/alignment-fixtures
python3 -m alignment_lab suite /tmp/alignment-fixtures/singing.json --device cpu --threads 2 --warm 2 \
  --model heuristic --model mms-fp32 --model mms-q8 --model hubert-ja-q4 > singing-cpu.json
```

FLEURS の2件は [FLEURS のデータカード][fleurs] の `ja_jp` と `en_us` の validation
を使います。リビジョン `70bb2e84b976b7e960aa89f1c648e09c59f894dd`、各先頭行の
音声と `raw_transcription` を取得し、算用数字を空白に置換したテキストを使いました。
`kind` を `speech` にしたマニフェストを用意し、HuBERT は日本語のケースに指定します。
FLEURS は CC BY 4.0、著作者は Google の FLEURS コーパス作成チームです。

| ONNX ファイル | SHA-256 |
| --- | --- |
| `model.fp32.onnx` | `26b2c0ba8f11ed045424ae1a70bc0b76f080952fb62fab5a37b3b39d82f08391` |
| `model.q8.onnx` | `bef3271b8f4496fd173da1d2392c4bc7d496e0245cf1e8700481f812795b4abe` |
| `onnx/model_q4.onnx` | `0151f5bd899a86aab0d34c06dba74689e60a95e6dce0a6642b6c005b74aef630` |

[mms]: https://huggingface.co/romara-labs/mms-300m-1130-forced-aligner-ONNX/tree/f515b917cf1f4f14d12a4768940cd8b4eb3e2119
[beat]: https://github.com/Kdroidwin/MusicBeatSP/blob/830e4cdcb111e2aa5214dd55b45c8ef8f0f786bb/app/src/main/java/com/music/bitchord/data/lyrics/CtcModelStore.kt
[hubert]: https://huggingface.co/air-air/japanese-hubert-base-phoneme-ctc-v4-ONNX/tree/a36617c5fbf7cfde02caa067716961c03cf4d87e
[beatrice]: https://huggingface.co/prj-beatrice/japanese-hubert-base-phoneme-ctc-v4/tree/f5fe07043bcb0b77a86faf72ac6d8fc1ae558f99
[qwen]: https://huggingface.co/Qwen/Qwen3-ForcedAligner-0.6B/tree/c7cbfc2048c462b0d63a45797104fc9db3ad62b7
[qwen-paper]: https://arxiv.org/abs/2601.21337
[shiro]: https://github.com/wavtechyukky/pyshiro
[sofa]: https://github.com/qiuqiao/SOFA
[singalign]: https://huggingface.co/pymaster/SingAlign
[whisperx]: https://github.com/m-bain/whisperX/blob/main/whisperx/alignment.py
[whispercpp]: https://github.com/ggml-org/whisper.cpp
[romaji]: https://github.com/Xiantaidu/RomajiASR
[cjkalign]: https://github.com/ijuinryukichi/lyric-align
[lamptales]: https://github.com/LampTales/lyric_align
[ffmpeg]: https://ffmpeg.org/legal.html
[jsut]: https://sites.google.com/site/shinnosuketakamichi/publication/jsut-song
[fleurs]: https://huggingface.co/datasets/google/fleurs
