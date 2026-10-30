# ADXL345 — 3 軸加速度センサ

> MEMS の 3 軸加速度センサ。±2/4/8/16 g のレンジと 32 段の FIFO、タップ・自由落下・
> 静止/活動の検出器を内蔵する。I2C アドレスは配線で `0x53` と `0x1D` のどちらかになる。
>
> **確度**: Analog Devices の datasheet に基づく。ただし `chips/adxl345.yaml` が指す
> analog.com の URL は 2026-08-15 時点で取得できず（TLS 層で自動取得が拒否される）、
> **M5Stack がミラーしている Rev E（2015-06）の PDF** を読んで書いた。カタログが
> 記録している版は Rev G（2022-05）で、**版が違う**。本文のページ番号はすべて Rev E のもの。
> 実機のキャプチャは無く、記述はすべて紙の上の話である。

## これは何か

3 軸それぞれの加速度を 16bit（実効 10〜13bit）のディジタル値で返す MEMS センサ。
測っているのは「センサに掛かっている加速度」であって、静止していれば重力そのものが
出てくる。

できること:

- X/Y/Z の加速度を `0x32`〜`0x37` の 6 バイトから読む。フルレンジは `±2 / ±4 / ±8 / ±16 g`
  の 4 段（p27, Table 21）。
- 分解能は 2 通り。10bit 固定モードではレンジごとに分解能が変わり、full resolution モードでは
  レンジによらず 3.9 mg/LSB（typ）に保たれ、±16 g で 13bit になる（p4, Table 1）。
- 出力レートは 0.1 Hz〜3200 Hz（p4）。
- 32 段の FIFO を持ち、bypass / FIFO / stream / trigger の 4 モードで動く（p27, Table 22）。
- シングルタップ・ダブルタップ・活動（activity）・静止（inactivity）・自由落下（free-fall）を
  チップ側で判定し、INT1/INT2 の 2 本のピンに割り付けられる（p20）。

**できないこと・持っていないもの**:

- **姿勢は分からない。** ジャイロも地磁気センサも無いので、方位（ヨー）は原理的に出せない。
  静止時に重力方向から傾き 2 軸を求めることはできるが、動いている間の傾きは求まらない。
- **重力と運動加速度を区別できない。** 出てくるのは両者の和である。分離したければホスト側で
  フィルタを掛けるか、他のセンサと組み合わせるしかない。
- **g の値は返さない。** 返るのは LSB カウントで、`DATA_FORMAT`（`0x31`）の設定に応じて
  ホスト側でスケールを掛ける必要がある。
- 温度センサを持たない。0 g オフセットの温度ドリフト（X,Y で ±0.4 mg/℃、Z で ±1.2 mg/℃、p4）は
  自前では補正できない。
- タイムスタンプを持たない。FIFO から読んだサンプルが「いつの」ものかはホストが管理する。
- **自分のアドレスを名乗れない。** `0x53` か `0x1D` かは ALT ADDRESS ピンの配線で決まり、
  レジスタからは読めない。

**電気的な現実**:

- 電源 Vs は **2.0〜3.6 V（typ 2.5 V）**、I/O 電源 VDD I/O は 1.7 V〜Vs（p4）。**5 V は掛けられない。**
  M5Stack Unit Accel の Grove 赤線は 5 V なので、ユニット基板側で降圧している。
- 消費電流は測定モードで 140 µA（ODR ≥ 100 Hz）、ODR < 10 Hz で 30 µA、スタンバイで 0.1 µA（p4）。
- **I2C で使うには `CS` を VDD I/O に固定しなければならない**（p15）。内部プルアップ/プルダウンが
  無いので、`CS` も ALT ADDRESS も浮かせると状態が定まらない（p18）。
- **バス速度が ODR の上限を決める。** 400 kHz で最大 800 Hz、100 kHz で最大 200 Hz。
  これを超える ODR を設定するとサンプルが落ちる（p18）。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x53`（ALT ADDRESS = GND）/ `0x1D`（ALT ADDRESS = VDD I/O） |
| 8bit 表記 | `0x53` → write `0xA6` / read `0xA7`、`0x1D` → write `0x3A` / read `0x3B`（p18） |
| アドレス変更 | 不可（ピンで決まる。ソフトからは変えられない） |
| 電源 | Vs 2.0〜3.6 V（typ 2.5 V）、VDD I/O 1.7 V〜Vs |
| インタフェース | I2C（100 / 400 kHz）または SPI 3/4 線。`CS` = High で I2C |
| レジスタアドレス | 8bit。読み書きともポインタ 1 バイト |
| バイト順 | 各軸 16bit は **LSB first**（`DATAx0` が下位）、2 の補数（p27） |
| 分解能 | 10bit 固定 / full resolution（3.9 mg/LSB typ、±16 g で 13bit） |
| ODR | 0.1〜3200 Hz。既定は 100 Hz（`BW_RATE` = `0x0A`） |
| FIFO | 32 段 ×（X,Y,Z）。既定は bypass（FIFO 停止） |
| 割り込み | INT1 / INT2 の 2 本。既定はアクティブ High、push-pull |
| デバイス ID | `0x00` を読むと `0xE5` |
| 電源投入時 | **スタンバイ**（`POWER_CTL` = `0x00`、Measure = 0） |
| 消費電流 | 140 µA（ODR ≥ 100 Hz）/ 30 µA（ODR < 10 Hz）/ 0.1 µA（スタンバイ） |
| 起動時間 | 1.4 ms（ODR = 3200 Hz、p4） |
| 搭載製品 | M5Stack Unit Accel (U056) — `0x53` |

## レジスタ

主要なものだけ。全体は datasheet p23 の Table 19 にある。

| アドレス | 名前 | R/W | リセット値 | 意味 |
|---|---|---|---|---|
| `0x00` | DEVID | R | `0xE5` | 固定のデバイス ID |
| `0x1D` | THRESH_TAP | R/W | `0x00` | タップ閾値。62.5 mg/LSB |
| `0x1E`-`0x20` | OFSX/Y/Z | R/W | `0x00` | 軸ごとのオフセット。2 の補数、15.6 mg/LSB（`0x7F` = 2 g、p24） |
| `0x21`-`0x23` | DUR / Latent / Window | R/W | `0x00` | タップの時間パラメータ |
| `0x24`-`0x26` | THRESH_ACT / THRESH_INACT / TIME_INACT | R/W | `0x00` | 活動・静止の閾値と時間 |
| `0x27` | ACT_INACT_CTL | R/W | `0x00` | 活動・静止に参加する軸 |
| `0x28`-`0x29` | THRESH_FF / TIME_FF | R/W | `0x00` | 自由落下の閾値（62.5 mg/LSB）と時間（5 ms/LSB） |
| `0x2A` | TAP_AXES | R/W | `0x00` | タップに参加する軸、suppress |
| `0x2B` | ACT_TAP_STATUS | R | `0x00` | どの軸が活動/タップの発生源か |
| `0x2C` | BW_RATE | R/W | `0x0A` | 出力レートと低消費電力ビット |
| `0x2D` | POWER_CTL | R/W | `0x00` | link / AUTO_SLEEP / **Measure** / sleep / wakeup |
| `0x2E` | INT_ENABLE | R/W | `0x00` | 割り込み出力の許可 |
| `0x2F` | INT_MAP | R/W | `0x00` | `0` = INT1、`1` = INT2 |
| `0x30` | INT_SOURCE | R | `0x02` | 発生要因。**読むと一部が消える**（後述） |
| `0x31` | DATA_FORMAT | R/W | `0x00` | SELF_TEST / SPI / INT_INVERT / FULL_RES / Justify / Range |
| `0x32`-`0x37` | DATAX0..DATAZ1 | R | `0x00` | X/Y/Z 各 16bit、LSB first、2 の補数 |
| `0x38` | FIFO_CTL | R/W | `0x00` | FIFO モード、trigger 先、watermark サンプル数 |
| `0x39` | FIFO_STATUS | R | `0x00` | FIFO_TRIG と現在の格納数 |

`0x01`〜`0x1C` は **Reserved で、触ってはいけない**（p23 の表に "do not access" と明記）。

`INT_SOURCE`（`0x30`）と `INT_ENABLE`（`0x2E`）と `INT_MAP`（`0x2F`）はビット配置が同じ:

| D7 | D6 | D5 | D4 | D3 | D2 | D1 | D0 |
|---|---|---|---|---|---|---|---|
| DATA_READY | SINGLE_TAP | DOUBLE_TAP | Activity | Inactivity | FREE_FALL | Watermark | Overrun |

`DATA_FORMAT`（`0x31`）の Range ビット（D1:D0）: `00` = ±2 g、`01` = ±4 g、`10` = ±8 g、`11` = ±16 g。
D3 の FULL_RES を立てるとレンジによらず 3.9 mg/LSB になる。

**転送の形**: レジスタポインタ 1 バイトを書き、repeated START を打って必要な長さを読む。
連続アドレスは自動でインクリメントされる（p18, Figure 41 の multiple-byte read）。

## 典型的な使い方

1. `0x00`（DEVID）を読む。`0xE5` が返れば ADXL345 で、アドレスも当たっている。
2. `0x31`（DATA_FORMAT）にレンジと分解能を書く。例: `0x0B` で ±16 g かつ full resolution。
3. `0x2C`（BW_RATE）に出力レートを書く。**バス速度に対して高すぎる ODR を選ばない**
   （400 kHz なら 800 Hz まで、100 kHz なら 200 Hz まで）。
4. FIFO を使うなら `0x38`（FIFO_CTL）にモードと watermark を書く。使わないなら既定の bypass のまま。
5. 割り込みを使うなら、**先に** `0x2E`（INT_ENABLE）を 0 のまま各機能を設定し、
   `0x2F`（INT_MAP）で出力ピンを決めてから `0x2E` を立てる（p20 の推奨手順）。
6. **`0x2D`（POWER_CTL）の Measure ビット（D3）を 1 にする。** ここまでやって初めて測定が始まる。
   `0x08` を書けばよい。
7. 1.4 ms 待ってから `0x32` を先頭に **6 バイトまとめて**読む。1 バイトずつ読むと、
   読んでいる途中で値が更新されて軸間で辻褄が合わなくなる（p27 の推奨）。
8. 16bit を組み立てる。`DATAx0` が下位、`DATAx1` が上位、2 の補数。g に直すには
   full resolution なら値 × 3.9 mg。

FIFO から読む場合も窓口は同じ `0x32`〜`0x37` で、6 バイト読むごとに 1 サンプルが pop する。
残数は `0x39`（FIFO_STATUS）の下位 6bit。

## 利用上の注意

- **リセット直後は測定していない。** `POWER_CTL`（`0x2D`）のリセット値は `0x00` で Measure = 0、
  つまりスタンバイである（p26）。この状態で `0x32`〜`0x37` を読むと、リセット値の
  `0x00` がそのまま返るか、更新されない古い値が返るだけで、**加速度ではない**。
  「読めているのに全部 0」の原因はほぼこれ。

- **`INT_SOURCE`（`0x30`）は読むと消えるが、消え方が 2 系統ある。** ここが一番間違えやすい。
  - DATA_READY / Watermark / Overrun の 3 ビットは、**データレジスタ（`0x32`〜`0x37`）を
    読むことで**クリアされる。`INT_ENABLE` の設定と無関係に常にセットされる。
  - それ以外（SINGLE_TAP / DOUBLE_TAP / Activity / Inactivity / FREE_FALL）は、
    **`INT_SOURCE` 自身を読むことで**クリアされる（p26）。

  つまり **読む順番が結果を変える**。
  - `INT_SOURCE` を読む前にデータレジスタを読むと、DATA_READY が落ちた後の `INT_SOURCE` が返る。
  - タップや活動の発生軸を知りたいなら、**`ACT_TAP_STATUS`（`0x2B`）を `INT_SOURCE` より先に
    読まなければならない**。datasheet は明示的に "The ACT_TAP_STATUS register should be read
    before clearing the interrupt" と書いている（p25）。順序を逆にすると、割り込みは消えたのに
    どの軸だったかが分からない、という状態になる。
  - **2 か所から `INT_SOURCE` を読むと片方が取りこぼす。** 読む場所は 1 か所に決める。

- **アドレスは `0x53` と `0x1D` の 2 通りで、しかも配線でしか決まらない。**
  ALT ADDRESS = GND で `0x53`、VDD I/O で `0x1D`（p18）。浮かせた場合の既定は無い。
  M5Stack Unit Accel は `0x53` 側。**バススキャンで `0x1D` に見つかったなら別の基板である。**

- **`0x53` は本カタログの `eeprom`（M5Stack Unit VMeter の校正 EEPROM）と衝突する。**
  Unit Accel と Unit VMeter を同じバスに繋ぐと両方 `0x53` で応答して壊れる。
  ADXL345 側のアドレスはソフトで動かせないので、回避するにはどちらかを別バスか
  I2C マルチプレクサ（`pca9548a` / `tca9548a`）の別チャンネルに逃がすしかない。

- **`0x01`〜`0x1C` は Reserved。** datasheet が "do not access" と書いている。スキャン目的でも
  舐めない。

- **FIFO は 1 バイト読みでサンプルを壊す。** FIFO モードでデータレジスタを 1 バイトだけ読むと、
  そのサンプルの残りのバイトは失われる（p21）。必ず 6 バイトのバースト読みにする。
  なお「FIFO の pop 完了に 5 µs 必要」という制約は SPI 向けの話で、
  I2C では通信速度が十分遅いので問題にならないと datasheet 自身が断っている（p21）。

- **trigger モードは 1 回きり。** 追加のトリガは、いったん bypass に落としてから trigger に
  戻すまで認識されない。しかも bypass にすると FIFO が消えるので、**データを読んでから**
  モードを戻す（p21）。

- **sleep / AUTO_SLEEP / link ビットをクリアするときは、いったん standby に落としてから
  measurement に戻す。** そうしないと直後の数サンプルにノイズが乗る（p25-26 で 3 回繰り返し
  注意されている）。

- **0 g オフセットは個体差。** X,Y で ±150 mg、Z で ±250 mg（p4, Table 1）。感度も ±1.0 %
  ばらつく。**個体ごとにキャリブレーションしないと、水平に置いても 0 にならないし
  1 g にもならない。** `OFSX/Y/Z`（`0x1E`-`0x20`）でチップ側に補正値を持たせられる。

- **書き込み中にデバイスが返すデータは無視する。** I2C / SPI いずれでも、書き込み中に
  ADXL345 から出てくるデータには意味が無い（p15）。

- **ODR とバス速度の関係を守る。** 400 kHz で 800 Hz 超、100 kHz で 200 Hz 超の ODR を
  設定すると、サンプル落ちや余分なノイズという形で現れる（p18）。エラーにはならないので
  気づきにくい。

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| レジスタ一覧（アドレス・型・リセット値） | datasheet p23（REGISTER MAP, Table 19） |
| 各レジスタのビット定義 | datasheet p24-27（REGISTER DEFINITIONS） |
| I2C のアドレス選択・転送形式・タイミング | datasheet p18-19（I2C, Table 11/12, Figure 40/41） |
| `CS` の扱いと SPI との切り替え | datasheet p15（SERIAL COMMUNICATIONS） |
| 割り込みの意味と消え方 | datasheet p20（INTERRUPTS）、p26（Register 0x30） |
| FIFO の 4 モードと読み出し規則 | datasheet p21（FIFO, Retrieving Data from FIFO） |
| 電源・感度・オフセット・ノイズの数値 | datasheet p4-5（SPECIFICATIONS, Table 1） |
| 消費電力モードと自動スリープ | datasheet p14（POWER SAVINGS、Standby Mode）、p25-26（POWER_CTL） |
| オフセット較正の手順 | datasheet p30（OFFSET CALIBRATION） |
| セルフテストの使い方 | datasheet p31（USING SELF-TEST） |
| 機械可読なレジスタマップ | [`profiles/adxl345.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/adxl345.yaml) |
| アドレスと搭載製品 | [`chips/adxl345.yaml`](../../chips/adxl345.yaml)、[`products/m5stack-u056.yaml`](../../products/m5stack-u056.yaml) |
| 本リポジトリの probe が何を触るか | [`scenarios/adxl345/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/adxl345/p0.yaml)、`collection/sketches/adxl345__characterize/adxl345__characterize.ino` |
| ミラーされた datasheet（Rev E） | [M5Stack OSS の ADXL345_en.pdf](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/ADXL345_en.pdf) |
| ユニットの仕様・寸法 | [M5Stack Unit Accel 製品ページ](https://docs.m5stack.com/en/unit/accel) |
| 実測（あれば） | `observations/adxl345/`（現時点で存在しない） |

## 出典と確度

- **一次資料**: Analog Devices *ADXL345 Data Sheet* **Rev. E（2015-06）、全 40 ページ**。
  `chips/adxl345.yaml` が記録している
  `https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL345.pdf` は
  **2026-08-15 時点で取得できなかった**（`tools/datasheet_text.py` も curl も TLS 層で
  切断される）。代わりに M5Stack がミラーしている
  `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/ADXL345_en.pdf`
  を取得し、pypdf でテキスト化して読んだ。本文中のページ番号はすべてこの Rev E のもの。
- **版の違い**: カタログの記録は Rev G（2022-05）。**Rev E → Rev G の変更点は確認していない。**
  レジスタマップと電気特性が変わっている可能性は低いが、確かめていないという意味で **未確認**。
- レジスタのアドレス・ビット定義・リセット値・読み取りクリアの規則・電気特性は、
  すべて上記 datasheet の記述。
- アドレス `0x53` / `0x1D` の使い分けは datasheet p18 と `chips/adxl345.yaml` の両方。
  M5Stack Unit Accel が `0x53` であることは製品ページと `products/m5stack-u056.yaml`。
- `0x53` の衝突（`eeprom`）は `chips/*.yaml` のアドレス欄を突き合わせて確認した。
- **未確認**: 実機のキャプチャが無い。DEVID が本当に `0xE5` を返すか、リセット直後の
  各レジスタが表のとおりか、400 kHz で動くかは確かめていない。
  `scenarios/adxl345/p0.yaml` はこれらを read-only で確認する計画だが、まだ走っていない。
- **未調査**: 書き込み側の挙動（測定開始、レンジ変更、FIFO、割り込み設定）は
  probe が read-only のため一切触れていない。タップ・自由落下検出器の実際の効き方も未検証。
