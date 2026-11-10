# 収集

実デバイスの記録（`runs/`）は、このリポジトリが OEP の probe で取る。
ケースのコードは probe 自身の上で動き、同じ probe がその線をキャプチャする。
ケースが書く目印と結果の行は、キャプチャと同じ probe の時計に載る。
ビルド、書き込み、収集は、pytest（pytest-embedded-arduino-cli）で行う。

## 1. 構成

```text
cases/<case>/case.yaml          何を録るか。collect: {sketch, number, lines} でどのスケッチの何番かを指す（DATA_MODEL §3）
collect/                        収集の pytest の作業場所（pyproject.toml、uv.lock、conftest.py）
  libraries/ProbeCases/         共通のファームウェア: ケースを番号で起こす OEP のインターフェース、目印のログ
  devicedata_collect.py         共通の Python: キャプチャ、記録、復号、provenance、run の名前
  benches/<bench>.yaml          ベンチの事実: probe の OEP アドレス、録る線、ボード、台帳の名前と位置、プルアップ
  <vendor>/<sku>/               製品ごとのスケッチ（計測する実物 1 つにつき 1 つ）
    <sku>.ino と *.h            oep-probe-arduino の examples/Firmware/OepProbe の写し（版は sketch.yaml の OpenEmbeddedProbe）
    Cases.cpp                   ケースの関数と番号
    sketch.yaml                 probe のボードごとの profile（名前は OepProbe の profile に合わせる）
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
```

- `--port`: 書き込みに使う口（probe の USB-Serial/JTAG の側）。
- `--bench`: `collect/benches/<bench>.yaml`。
- `--bus-hz`: ケースを動かすバスの速さ（既定 100000）。
- `--runs-out`: 書き出し先（既定はこのリポジトリの `runs/`）。

モジュールごとに、プラグインがスケッチを一度ビルドして書き込む。そのあと、ケースごとに次を行う。

1. probe の lease を取る。
2. バスの速さを設定し、目印のログを消す。
3. 線を stream で取りながらケースを起こし、終わりを待つ。
4. `run.json` とキャプチャを書き、wireskein で復号する。
5. provenance を書き、run の名前に改める。

キャプチャか目印のログに欠けがあれば、そのケースは失敗にする。同じ転送の run がすでにあれば、新しいほうは捨てる。

録ったら `python3 tools/validate.py` を通してからコミットする。

## 3. 足す

- **ケース:** `Cases.cpp` に関数を書いて、番号の表に足す。`cases/<case>/case.yaml` を書き、`collect.sketch` と `collect.number` で指す。
  - 関数は `# <case>` で始めて `#` で閉じ、段階ごとに `## <phase>` と `##` を書く。phase の名前は case.yaml の `phases` と同じにする。
  - 段階の結果は 1 行で書く（`end=0 got=6 bytes=...`）。
  - 細かいことは生のキャプチャから分析できるので、目印は「何をしたか」が分かれば足りる。
- **製品:** `collect/<vendor>/<sku>/` を作る。
  - OepProbe の写し、`Cases.cpp`、`sketch.yaml`、`<sku>.py` を置く。
  - `Cases.cpp` の `oepUserSetup` は、`probe_cases::begin` に自分の名前空間のインターフェース名（48 バイト以内）を渡す。
- **ベンチ:** `collect/benches/<bench>.yaml` を書く。名前は oep-bench の台帳の略称。

## 4. 決まり

- probe の lease は、収集のあいだ持ち続ける。`bench.lock` は取らない（OEP の道具専用）。
- 写した OepProbe は、OpenEmbeddedProbe の版を上げるときに、その版の `examples/Firmware/OepProbe` から取り直す。
- oep-client-python と wireskein の版は `collect/pyproject.toml` で固定し、上げるときは意図して上げる。
