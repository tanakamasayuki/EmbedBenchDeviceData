# SHT30 — 温湿度センサ（SHT3x-DIS ファミリの標準精度品）

> 16bit の温度と湿度を、各ワードに CRC-8 を付けて返す固定機能センサ。測定完了の待ち方が
> 2 通り（ポーリング / クロックストレッチ）あり、どちらを選ぶかがホスト側の詰まりどころ。
>
> **確度**: Sensirion の datasheet（SHT3x-DIS, Version 7, 2022-12）に基づく。加えて本リポジトリでは
> **実測がある**: presence、soft reset、status + CRC、high / medium / low の polling 測定、
> medium / low の clock-stretch 測定は observation `sht30-p0-100k-nominal-1b9dbfe9f8c22329` と
> `sht30-p0-100k-nominal-29d9633ff0bd82a0` で確認済み。periodic mode・ART・heater・ALERT は
> **未実測**（datasheet のみ）。

## これは何か

Sensirion の CMOSens 技術による温湿度センサ。1 個の DFN パッケージに、湿度素子・温度素子・
A/D・線形化と温度補償の演算・I2C インタフェースが入っている。**出荷時に校正済みで、ホスト側に
補正係数を読ませる必要がない**（BMP280 や QMP6988 のような calibration register が無いのはこのため）。
返ってくる 16bit の値は、線形化と温度・電源電圧補正が済んだあとの値である。

SHT30 は SHT3x-DIS ファミリの**入門グレード**で、SHT31 / SHT35 は同じコマンドセットのまま精度だけが
高い。ソフトウェアから見た差は無い（型番を読み分ける手段もバス上には無い）。

できること:

- 単発（single shot）測定。repeatability を high / medium / low の 3 段から選べる。
  精度ではなく**ノイズと測定時間と消費電力**のトレードオフである（high 12.5 ms typ、low 2.5 ms typ）。
- 周期（periodic）測定。0.5 / 1 / 2 / 4 / 10 回毎秒で走らせ、`fetch data` で取り出す。
- ART（accelerated response time）で 4 Hz 固定の周期測定に入る。湿度の応答時間が約 2 倍改善する。
- ステータスレジスタの読み出しとクリア。リセット検出、直前コマンドの成否、書き込み CRC の成否が分かる。
- ヒータの ON / OFF。**測定のためではなく、素子が生きているかの妥当性確認用**。
- ADDR ピンでアドレスを 2 つから選べる。ピンを動的に切り替えれば 3 台以上を同じバスに載せられる。

できないこと・持っていないもの:

- **絶対圧・気圧は測れない。** 温度と湿度だけ。ENV-II / ENV-III が気圧センサ（BMP280 / QMP6988）を
  別に載せているのはこのため。
- **データレディを知らせるピンが無い。** ALERT ピンはあるが、これは「しきい値を越えた」を知らせる
  ものであってデータレディではない。しかも M5Stack の Unit では外部に出ていない。
- **periodic mode ではクロックストレッチを選べない。** ストレッチは単発測定のコマンドにしか無い。
- **測定値をチップ内に溜め込めない。** periodic mode でも保持されるのは最新の 1 組で、
  `fetch data` で読むと消える。ログを取りたければホストが読み続けるしかない。
- **不揮発の設定が無い。** ヒータもステータスもリセットで既定値に戻る。校正値の書き換えもできない。
- I2C アドレスをコマンドで変更する手段は無い（ADDR ピンだけ）。

電気的な現実:

- VDD は **2.15〜5.5 V**。5 V 系にも 3.3 V 系にも直結できる数少ないセンサで、M5Stack の Unit が
  Grove の 5 V をそのまま使えるのはこの範囲のおかげである（実測 run の provenance も 5.0 V）。
- 測定中 600 µA typ、待機 0.2 µA typ。SCL は 0〜1000 kHz まで許容（400 kHz を超えるときは
  datasheet Table 21 の条件を満たすこと）。
- **ADDR ピンをフローティングにしてはいけない。** Low で `0x44`、High で `0x45`。
- ヒータ ON 時は 3.6〜33 mW を消費し、当然その熱は自分の温度センサに乗る。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x44`（ADDR=GND、既定） / `0x45`（ADDR=VDD） |
| アドレス変更 | ADDR ピンのみ。コマンドでは変更できない |
| 電源 | 2.15〜5.5 V（M5Stack Unit は Grove の 5V をそのまま使用） |
| I2C 速度 | 0〜1000 kHz。実測は 100kHz で成立（400kHz は**未実測**） |
| コマンド長 | 16bit、MSB first。コマンド自体に 3bit の CRC が埋め込まれている |
| 応答フレーム | 温度 2 バイト + CRC 1 バイト + 湿度 2 バイト + CRC 1 バイト = **6 バイト** |
| CRC | CRC-8、多項式 `0x31`、初期値 `0xFF`、反転なし、最終 XOR `0x00` |
| 測定時間 | low 2.5 ms / medium 4.5 ms / high 12.5 ms（typ）。max はそれぞれ 4 / 6 / 15 ms |
| 精度（typ） | 温度 ±0.2 ℃（0〜65℃）、湿度 ±2 %RH |
| 分解能 | 0.01 ℃ / 0.01 %RH |
| 応答時間 | 湿度 τ63% = 8 s、温度 τ63% > 2 s |
| 割り込み | ALERT ピンはあるが Unit では未接続。実質ポーリング |
| 起動時間 | 電源投入後 tPU 最大 1 ms、soft reset 後 tSR 最大 1.5 ms |

## コマンド

すべて 16bit。MSB first で 2 バイト送る。

| コマンド | 意味 | 備考 |
|---|---|---|
| `0x2400` / `0x240B` / `0x2416` | 単発測定（high / medium / low）、**クロックストレッチ無効** | 実測済み |
| `0x2C06` / `0x2C0D` / `0x2C10` | 単発測定（high / medium / low）、**クロックストレッチ有効** | 実測済み（high のみ後述の問題あり） |
| `0x2032` / `0x2130` / `0x2236` / `0x2334` / `0x2737` ほか | 周期測定の開始（repeatability × 0.5/1/2/4/10 mps の 15 通り。上位バイトが mps、下位が repeatability） | 未実測。全 15 通りは datasheet Table 10 |
| `0x2B32` | ART（4 Hz の周期測定）開始 | 未実測 |
| `0xE000` | fetch data（周期測定の最新値を取り出す） | 未実測 |
| `0x3093` | break（周期測定を止めて単発モードへ戻る）。完了まで 1 ms | 未実測 |
| `0x30A2` | soft reset。tSR 最大 1.5 ms | 実測済み |
| `0xF32D` | ステータスレジスタ読み出し（2 バイト + CRC） | 実測済み |
| `0x3041` | ステータスのフラグをクリア | 実測済み |
| `0x306D` / `0x3066` | ヒータ ON / OFF | 未実測 |
| `0x0006` @ アドレス `0x00` | general call reset（**バス上の全デバイスがリセットされる**） | 未実測 |

**応答フレームの形（Sensirion 共通）**: 応答は必ず「16bit ワード（MSB first）+ CRC-8 1 バイト」の
3 バイト単位で並ぶ。単発測定なら `[温度上位, 温度下位, CRC, 湿度上位, 湿度下位, CRC]` の 6 バイト。
ステータスなら `[上位, 下位, CRC]` の 3 バイト。CRC は**直前の 2 バイトだけ**を対象に計算する
（多項式 `0x31` = x⁸+x⁵+x⁴+1、初期値 `0xFF`、入出力ビット反転なし、最終 XOR `0x00`。
検算値: CRC(`0xBEEF`) = `0x92`）。読み出し方向では CRC を読み飛ばしてもよいが、書き込み方向で
パラメータを送るときは CRC が必須で、間違っているとセンサはデータを受け付けない。

**CRC が落ちたときと、値がおかしいときは別の話である。** CRC 不一致は「バスの上でバイトが壊れた」
という意味で、疑うべきはプルアップ・配線長・バス速度・ノイズであってセンサではない。逆に
CRC が通っているのに値が変なら、それはセンサが本当にその値を返している（ヒータが入っている、
自己発熱している、まだ安定していない、ワード境界を取り違えている、など）。**バイト境界が 1 つずれると
全ワードの CRC が落ちる**ので、「全部の CRC が落ちる」ときはフレーム長の解釈を先に疑う。

**値の変換**（datasheet 4.13）:

- 温度 ℃ = −45 + 175 × S_T / (2¹⁶ − 1)
- 湿度 %RH = 100 × S_RH / (2¹⁶ − 1)

S_T、S_RH は符号なし 16bit の生値。実測の例では S_T = 26619、S_RH = 39527、つまり約 26.1 ℃ /
60.3 %RH だった。

**ステータスレジスタ**（`0xF32D` の 16bit）の主なビット: bit15 = alert pending、bit13 = ヒータ ON、
bit11 = RH tracking alert、bit10 = T tracking alert、bit4 = **リセット検出**、bit1 = 直前コマンドの
失敗、bit0 = 直前の書き込み CRC の失敗。クリアできるのは bit15 / 11 / 10 / 4 のみ。

## 典型的な使い方

単発測定（推奨）:

1. 電源投入から 1 ms 待つ。
2. `0x30A2`（soft reset）を送り、1.5 ms 待つ。状態を既知にするため。
3. `0xF32D` を送り、3 バイト読む。CRC を検算する。ここで**通信が成立しているかと、CRC の実装が
   合っているかを同時に確認できる**。bit4 が立っているのは直前にリセットしたからで正常。
4. `0x3041` を送ってフラグをクリアする。
5. 測定コマンドを送る。**ここでポーリングかクロックストレッチかを選ぶ**（下記）。
6. 6 バイト読む。各ワードの CRC を検算してから変換する。
7. 次のコマンドまで最低 1 ms 空ける（datasheet 4 章の共通規定）。

**ポーリングで待つ場合**（`0x2400` / `0x240B` / `0x2416`）:

コマンドを送ったあと、読み出しヘッダを出す。まだ測定が終わっていなければセンサは**アドレスに
NACK を返す**。ACK が返るまで一定間隔で再試行し、返ったら 6 バイト読む。
「NACK が返る」は異常ではなく、このセンサの data-ready の伝え方そのものである。

**クロックストレッチで待つ場合**（`0x2C06` / `0x2C0D` / `0x2C10`）:

コマンドを送ったあと読み出しヘッダを出すと、センサはアドレスに ACK を返したうえで**SCL を Low に
引っ張ったまま保持する**。測定が終わると SCL を離し、そのまま 6 バイトを送ってくる。ホスト側の
コードは「読むだけ」で待ち時間の管理が要らない。ただし後述の落とし穴がある。

周期測定（**未実測**）:

1. 周期測定コマンド（例 `0x2130` = 1 mps / high）を送る。
2. 更新周期ごとに `0xE000`（fetch data）を送って 6 バイト読む。データが無ければ読み出しヘッダが
   NACK される。読み出すとデータメモリはクリアされる。
3. 他のコマンドを送る前に `0x3093`（break）で単発モードへ戻す。

## 利用上の注意

- **ホスト側のコントローラが、センサの変換時間より短い SCL-low タイムアウトを持っていることがある。**
  これが本リポジトリで実測された最大の落とし穴である。ESP32-S3（arduino-esp32 core 3.3.10、100 kHz、
  5.0 V）で `0x2C06`（high repeatability + クロックストレッチ）を実行すると、**6.759 ms で 0 バイトを
  返して読み出しを打ち切った**。同じ high repeatability を `0x2400` でポーリングした場合は
  **13.1 ms かけて 6 バイトを正常に取得**している（observation の `elapsed_us` は 13148 / 13149、
  pilot の記録は 13.101 ms）。ロジックアナライザで見た SCL の Low 保持は 11.0 ms で、
  つまり**センサは仕様どおり（tMEAS,h typ 12.5 ms）に動いており、先に諦めたのはホストのほう**である。
  medium（`0x2C0D`、3.7 ms）と low（`0x2C10`、2.5 ms 前後）は同じ条件で成功しているので、
  この controller のしきい値は 3.8 ms と 6.76 ms の間にある。2 回の run で `blocking_us` が
  どちらも 6759 だったことから、タイムアウト値は決定的（偶発的なノイズではない）。
  → **これはセンサ故障ではない。** 症状は「high だけ 0 バイト」「high だけ CRC 以前に何も返らない」。
  対処は (a) high repeatability ではポーリング（`0x24xx`）を使う、(b) コントローラの SCL-low
  タイムアウトを延ばす、(c) repeatability を medium に落とす、のいずれか。
  なお打ち切った直後にもう一度読むと（実測では 741 µs 後）正しい 6 バイトが CRC 込みで取れている。
  測定自体は成立していて、取りこぼしたのは 1 回目の読み出しだけである。
- **ポーリング時の NACK を通信エラーとして扱わない。** 「測定がまだ終わっていない」の合図である。
  ライブラリによってはこれを I2C エラーとして上に投げるので、リトライ回数の上限で測定失敗に
  見えることがある。実測の probe も、意図的に準備前に 1 回読んで `not_ready_read` として記録している。
- **periodic mode ではクロックストレッチのコマンドは存在しない。** ストレッチ付きの `0x2Cxx` は
  単発測定専用。periodic では必ず `0xE000` の NACK でデータレディを判断する。
- **periodic mode 中に別のコマンドを送る前に `0x3093`（break）を出す。** 測定中に測定コマンドを
  重ねると挙動が定義されない。break は 1 ms で完了する。
- **ヒータは測定用ではない。** datasheet が "for plausibility checking only" と明記している。
  数℃の温度上昇が起きるので、ON にしたまま測った温湿度は使えない。ステータスの bit13 で
  ON かどうかを確認できる。リセット後は必ず OFF。
- **general call reset（`0x00` + `0x06`）はバス上の全デバイスを巻き込む。** 同じバスに他のセンサや
  ディスプレイが載っている構成では使わない。SHT30 だけを戻したいなら soft reset。
- **`0x44` に応答があっても SHT30 とは限らない。** SHT3x-DIS は既定 `0x44`、SHT4x も既定 `0x44`。
  ENV-III（SHT30）と ENV-IV（SHT40）はアドレスで区別できない。コマンド体系が違う
  （SHT3x は 16bit、SHT4x は 8bit）ので、`0xF32D` に応答するかどうかで見分けるのが実際的。
- **CRC を検算せずに使わない。** 6 バイトのうち 2 バイトは CRC で、これを飛ばして 4 バイトだけ
  読むと、境界のずれや取りこぼしに気づけない。読み飛ばしが許されるのは「速度のために最後の
  CRC で NACK して打ち切る」ケースだけである。
- **ADDR ピンは I2C トランザクションの途中で変えてはいけない。** START から通信終了までは
  レベルを保つ、というのが datasheet の唯一の制約である（3.4 節）。
- **1 コマンド送るごとに最低 1 ms 空ける。** これを守らないと次のコマンドが受理されない
  （datasheet 4 章冒頭）。

## どこに何が書いてあるか

datasheet は [SHT3x-DIS Datasheet Version 7](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf)（2022-12、22 ページ）。

| 知りたいこと | 参照先 |
|---|---|
| 精度・繰り返し性・応答時間・ドリフト | 1 章 Sensor Performance、p.2（Table 1 / Table 2）、グラフ p.3–5 |
| 推奨動作範囲（5〜60℃ / 20〜80 %RH） | 1.1 節、p.6 |
| 電源電圧・消費電流・ヒータ電力 | 2.1 節 Table 3、p.6 |
| **測定時間 tMEAS（low 2.5 / medium 4.5 / high 12.5 ms typ）** | 2.2 節 Table 4、p.7 |
| ピン配置・ADDR / ALERT / nRESET の扱い | 3 章 Table 7、p.8。ADDR は 3.4 節 Table 8、p.8–9 |
| 16bit コマンドの形・1 ms 間隔の規定・CRC の位置づけ | 4 章冒頭、p.9 |
| **単発測定コマンド一覧（`0x24xx` / `0x2Cxx`）** | 4.3 節 Table 9、p.9–10 |
| **NACK ポーリングとクロックストレッチの説明** | 4.4 節、p.10 |
| 周期測定コマンド一覧（15 通り） | 4.5 節 Table 10、p.10–11 |
| fetch data `0xE000` | 4.6 節 Table 11、p.11 |
| ART `0x2B32` | 4.7 節 Table 12、p.11 |
| break `0x3093` | 4.8 節 Table 13、p.11–12 |
| soft reset `0x30A2` / general call / インタフェースリセット | 4.9 節 Table 14・Table 15、p.12 |
| ヒータ `0x306D` / `0x3066` | 4.10 節 Table 16、p.12–13 |
| **ステータスレジスタのビット定義** | 4.11 節 Table 18、p.13。クリア `0x3041` は Table 19 |
| **CRC-8 のパラメータと検算値** | 4.12 節 Table 20、p.13–14 |
| **生値 → ℃ / %RH の変換式** | 4.13 節、p.14 |
| I2C タイミング（1 MHz までの条件） | 4.14 節 Table 21、p.14–15 |
| 機械可読なコマンド定義・フレーム・timing | [`profiles/sht30.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/sht30.yaml) |
| チップの素性・アドレス・datasheet リンク | [`chips/sht30.yaml`](../../chips/sht30.yaml) |
| 製品（ENV-II / ENV-III） | [`products/m5stack-u001-b.yaml`](../../products/m5stack-u001-b.yaml) / [`products/m5stack-u001-c.yaml`](../../products/m5stack-u001-c.yaml) |
| **実測データ（測定値・CRC・タイミング）** | [`observations/sht30/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/observations/sht30/) の 2 ファイル |
| 実測の生波形 / デコード結果 | `captures/raw/sht30__sht30__characterize__100k__nominal__*.sr` と `captures/decoded/` の同名 `.jsonl` |
| clock stretch 問題の経緯と扱い | [`docs/pilots/m5stack-u001-c.md`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/docs/pilots/m5stack-u001-c.md) |
| 収集シナリオ（何を測る / 何を見送る） | [`scenarios/sht30/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/sht30/p0.yaml) |
| 実測に使った probe のソース | `collection/sketches/sht30__characterize/sht30__characterize.ino` |
| 比較用ライブラリ | [`libraries/sensirion-arduino-sht.yaml`](../../libraries/sensirion-arduino-sht.yaml) / [`libraries/m5stack-m5unit-env.yaml`](../../libraries/m5stack-m5unit-env.yaml) |

## 出典と確度

- コマンド、フレーム、CRC、変換式、タイミング仕様: **SHT3x-DIS Datasheet Version 7（2022-12）**。
  M5Stack が配布しているのは Version 6（2019-02）で、本ガイドが参照した範囲では章立ても
  コマンドも同じである。
- **実測で確認済み**（observation `sht30-p0-100k-nominal-1b9dbfe9f8c22329` および
  `sht30-p0-100k-nominal-29d9633ff0bd82a0`、product `m5stack-u001-c`、specimen `enviii-001`、
  100 kHz、5.0 V、ESP32-S3 / esp32 core 3.3.10）:
  - `0x44` がアドレス ACK を返すこと。
  - soft reset `0x30A2` → status `0xF32D`（3 バイト、CRC OK、値 0）→ clear `0x3041` の一連が通ること。
  - high / medium / low のポーリング測定が 6 バイトを返し、温度・湿度とも CRC が通ること。
  - medium / low のクロックストレッチ測定が成立すること。
  - **high のクロックストレッチ読み出しが ESP32-S3 側で 6.759 ms 打ち切りになること**、および
    その直後の再読み出しで正しい値が取れること。
- **未確認**: 400 kHz での動作、periodic mode 一式（`0x20xx`〜`0x27xx`、`0xE000`、`0x3093`）、
  ART `0x2B32`、ヒータ `0x306D` / `0x3066`、ALERT のしきい値書き込みとピンの挙動、
  `0x45`（ADDR=VDD）側のアドレス。`profiles/sht30.yaml` の `coverage` でも
  periodic-mode と heater は `deferred` のままである。
- **注意**: 上記 observation は pilot データである。統一前の手書き probe で、収集方法そのものを
  詰めている最中に取ったもので、恒久的な characterization の正本ではない
  （[`docs/pilots/m5stack-u001-c.md`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/docs/pilots/m5stack-u001-c.md)）。値の傾向は信用してよいが、
  「1 個体・1 回の run」であることは忘れないこと。とくに 6.759 ms というタイムアウト値は
  この MCU・この core バージョン固有の数字であって、SHT30 の仕様ではない。
