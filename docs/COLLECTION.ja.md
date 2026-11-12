# 収集

実デバイスの記録（`runs/`）は、このリポジトリが OEP の probe で取る。
ケースのコードは probe 自身の上で動き、同じ probe がその線をキャプチャする。
probe のファームは oep-probe-arduino の `OepFirmware.h` そのもので、スケッチはその上にケースのインターフェースを足すだけである。
ケースが書く目印と結果の行は、キャプチャと同じ probe の時計に載る。
ビルド、書き込み、収集は、pytest（pytest-embedded-arduino-cli）で行う。

## 1. 構成

```text
cases/<case>/case.yaml          何を録るか。collect: {lines, sketches: [{sketch, number}]} で、どのスケッチの何番かを指す（DATA_MODEL §3）
collect/                        収集の pytest の作業場所（pyproject.toml、uv.lock、conftest.py）
  libraries/ProbeCases/         共通のファームウェア: ケースを番号で起こすインターフェース（oep::SimpleInterface）、ワーカー、目印のログ
  libraries/DeviceCases/        部品ごとのケース（部品のライブラリを使わないもの）と wire-scan。その部品が載る製品のスケッチが呼ぶ
  devicedata_collect.py         共通の Python: キャプチャ、記録、復号、provenance、run の名前
  benches/<bench>.yaml          ベンチの事実: probe の OEP アドレス、録る線、ボード、台帳の名前と位置、プルアップ。ユニットを付け替えるベンチは、付けたユニットごとに 1 つ（p4x-grove-<sku>）
  <vendor>/<sku>/               製品ごとのスケッチ（計測する実物 1 つにつき 1 つ）
    <sku>.ino                   OepFirmware.h を読み込み、casesBegin() のあとで oep::firmware.begin() を呼ぶ
    Cases.cpp                   ケースの番号（DeviceCases の関数、ライブラリのケース）、使うピン、casesBegin()
    sketch.yaml                 probe のボードごとの profile（名前と fqbn は oep-probe-arduino の OepProbe の profile に合わせる）
    <sku>.py                    その製品のケースを録る pytest
```

`<sku>.py` に `test_` を付けないのは、`pytest` を素で走らせても収集が始まらないようにするため。
収集は probe を書き換え、`runs/` に書き込むので、ファイルを名指ししたときだけ走らせる。

## 2. 録る

```sh
cd collect
uv sync
uv run pytest m5stack/u001-c/u001-c.py --profile esp32p4x \
  --port /run/board-identify/by-id/esp32-series-80f1b2d719c5 --bench p4x-grove [-k sht30] [--bus-hz 400000]
# ほかのユニット: m5stack/<sku>/<sku>.py と --bench p4x-grove-<sku>（そのユニットだけを Grove に付けて）
```

- `--port`: 書き込みに使う口（probe の USB-Serial/JTAG の側）。
- `--bench`: `collect/benches/<bench>.yaml`。
- `--bus-hz`: ケースを動かすバスの速さ（既定 100000）。
- `--runs-out`: 書き出し先（既定はこのリポジトリの `runs/`）。

モジュールごとに、プラグインがスケッチを一度ビルドして書き込む。そのあと、ケースごとに次を行う。

1. probe の lease を取る。
2. バスの速さを設定し（probe のワーカーの上でバスを作り直す）、目印のログを消す。
3. 線を stream で取りながら、ケースをジョブとして起こし、終わりを待つ（oep_client の `call_wait`）。
4. `run.json` とキャプチャを書き、wireskein で復号する。
5. provenance を書き、run の名前に改める。

キャプチャか目印のログに欠けがあれば、そのケースは失敗にする。
ケースの関数が 0 以外を返したときは、その値を run の notes に残す（失敗にはしない）。
同じ転送の run がすでにあれば、新しいほうは捨てる。

録ったら `python3 tools/validate.py` を通してからコミットする。

## 3. 足す

- **ケース:** 部品を直接叩くケースは `libraries/DeviceCases/` に `<部品>::<ケース>(TwoWire &, addr)` として書き、その部品が載る製品のスケッチの `Cases.cpp` から番号を付けて呼ぶ。部品のライブラリを使うケースは、その製品の `Cases.cpp` に書く。
  - `cases/<case>/case.yaml` を書き、`collect.sketches` に録るスケッチとその番号を並べる。targets は製品ごとに書く（run には、録ったスケッチの製品の targets だけが入る）。
  - 同じ部品の同じケースは、どの製品で録っても同じ case にする（run の名前と provenance で製品が分かれる）。
  - 関数は `# <case>` で始めて `#` で閉じ、段階ごとに `## <phase>` と `##` を書く。phase の名前は case.yaml の `phases` と同じにする。
  - 段階の結果は 1 行で書く（`end=0 got=6 bytes=...`）。
  - 細かいことは生のキャプチャから分析できるので、目印は「何をしたか」が分かれば足りる。
  - 時間を計るケースは、段階の中に出来事の目印（3 段目）を書く。呼び出しが戻った直後に書き、その呼び出しの転送が何を意味したかだけを言う。
    - `start(timing, "name=value ...")`: この転送から、knowledge の timing の key の計測が始まる。条件は名前と値で書く（バスの速さは hz として足される）。
    - `before(timing)`: この転送は、出来事の前の状態を見た（拒まれた読み出し、立っていないビットなど）。
    - `after(timing[, k])`: この転送が、出来事の後の状態を見た最初のもの。同じ形の 1 つ前の転送は前の状態を見たことになるので、ポーリングでは `before` を書かなくてよい。k は連なり（ノーマルモードの k 番目のサンプルなど）の番号。
    - `afterRelease(timing)`: 部品がこの転送のクロックストレッチを放したのが出来事（ストレッチしなかった読み出しは、始まりの時点の after）。
  - `tools/measure.py` が、目印を直前の転送に吸着させ、時刻は線から取って、条件ごとに窓を出す。部品の意味は目印の側にしかないので、分析機は部品ごとに書かない。
    - 吸着先は目印の前に終わった最後の転送で、3 ms 以内でなければならない。同じ計測の中で同じ転送に 2 度は吸着しない。呼び出しが線に何も出さなかったときは、分析が止まる。
    - 計測は、同じ timing の次の `start`、または段階の終わりまで続く。
- **製品:** `collect/<vendor>/<sku>/` を作る。
  - `<sku>.ino`、`Cases.cpp`、`sketch.yaml`、`<sku>.py` を置く（u001-b や u009 が見本）。スケッチの `<vendor>/<sku>` は `products/<vendor>-<sku>.yaml` に当たる。
  - `Cases.cpp` の `casesBegin()` は、使うピンを `oep::firmware.onSetup` で `oep::owners::kSketch` として取り、`probe_cases::begin` に自分の名前空間のインターフェース名（48 バイト以内）を渡す。
- **ベンチ:** `collect/benches/<bench>.yaml` を書く。名前は oep-bench の台帳の略称（ユニットを付け替えるベンチは `<略称>-<sku>`）。

## 4. 決まり

- probe の lease は、収集のあいだ持ち続ける。`bench.lock` は取らない（OEP の道具専用）。
- 割込みを使う周辺（Wire、ESP-IDF の i2c_master など）は、ワーカーの上で作る。つまり `configure` かケースの中で begin し、`setup()` や `casesBegin()` では begin しない。
  - 割込みは begin したコアに付く。ESP32-P4 の probe では loop のコア（コア 1）に endpoint と USB の割込みが集まる。
  - そこに I2C master の割込みが付くと、割込みが遅れる。アドレスの NACK が成功として返り、そのあと落ちることもある。
  - ワーカー（コア 0）で作れば起きない（`esp32p4-i2c-nack-answers`）。
- OpenEmbeddedProbe（sketch.yaml）と oep-client-python（`collect/pyproject.toml`）は、ジョブとログの口（oep::SimpleInterface の job / addWorker / addLog、oep_client.call）が入った 0.0.54 以降を使う。
- 依存の版は sketch.yaml と `collect/pyproject.toml` で固定し、上げるときは意図して上げる。
  - 区切り（push の前、長い作業の後）で公開の版と比べる。sketch.yaml は pytest-embedded-arduino-cli の TESTING_ADVANCED.ja.md「sketch.yaml の版を保守する」に従う。
  - 上げたら全スケッチをビルドし、ENV III を録り直して前の run と転送が変わらないことを確かめる。
