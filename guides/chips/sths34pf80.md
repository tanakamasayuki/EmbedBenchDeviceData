# STHS34PF80 — 赤外（TMOS）人感 / 動体センサ

> 焦電素子ではなくサーモパイル型（TMOS）の赤外センサ。**静止している人も検出できる**のが
> 従来の PIR との違い。在室・動体・環境温度急変の 3 つの判定をチップ内のアルゴリズムが行い、
> `FUNC_STATUS`（`0x25`）のフラグとして返す。
>
> **確度**: STMicroelectronics の datasheet **DS13916 Rev 2（2023-07）** と
> アプリケーションノート **AN5867 Rev 2** に基づく。`chips/sths34pf80.yaml` が記録している
> st.com の URL は 2026-08-15 時点で取得できず（TLS 層で自動取得が拒否される）、
> **M5Stack がミラーしている同一版（Rev 2）の PDF** を取得して読んだ。
> 版はカタログの記録と一致している。実機のキャプチャは無い。

## これは何か

視野内の物体が出す赤外線（5〜20 µm）の量を 16bit で測り続けるセンサ。生の値そのものより、
チップ内蔵の「スマートディジタルアルゴリズム」が出す 3 つのフラグが主役である。

できること:

- **在室検出（presence）**: 視野内に熱源が居るかどうか。**動いていなくても検出できる**。
  2 段のローパスフィルタの出力差（`TPRESENCE`）を閾値と比較して判定する（AN5867 p19）。
- **動体検出（motion）**: 視野内で熱源が動いたかどうか。
- **環境温度急変検出（ambient temperature shock）**: センサ自身の周囲温度が急に変わったとき。
  誤検出の原因になる状況をホストに知らせるためのもの。
- 物体温度 `TOBJECT`（`0x26`/`0x27`）と環境温度 `TAMBIENT`（`0x28`/`0x29`）を 16bit・2 の補数で読める。
  `TAMBIENT` の感度は 100 LSB/℃、`TOBJECT` は 2000 LSB/℃（DS p5）。
- 出力レート 0.25 / 0.5 / 1 / 2 / 4 / 8 / 15 / 30 Hz（DS p25, Table 20）と、
  1 回だけ測るワンショットモード。
- 平均化サンプル数を 2〜2048 で選べ、ノイズが 90 → 10 LSBrms まで下がる（DS p23, Table 19）。
- 割り込みピン 1 本。データレディか、3 つのフラグの論理和のどちらかを出せる。

**できないこと・持っていないもの**:

- **人数を数えられない。距離も方向も出ない。** 視野（80°）の中に何かが居る、としか言わない。
- **人と人以外を区別しない。** 熱源であればストーブでも猫でも同じように反応する。
- **絶対温度計ではない。** `TOBJECT` が返すのは「視野内から届いた赤外線量」であって、
  距離・放射率・視野の占有率がすべて効く。`TOBJECT` を「対象物の温度 [℃]」に直す
  一般的な換算式は datasheet に無い。`TAMBIENT` の方は 100 LSB/℃ の温度計として使えるが、
  それは**センサ自身と熱結合した環境**の温度である。
- **画像は出ない。** 単素子であって、MLX90640 のようなサーモパイルアレイではない。
- **視野内が空でない状態で立ち上げると、アルゴリズムが狂う。** AN5867 は
  「パワーダウン → 連続モードへ移るときと、アルゴリズムをリセットするときは、
  視野が空でなければならない」と明記している（AN5867 p18）。
- **アドレスを変えられない。** `0x5A` 固定。ピンもレジスタも無い。
- **ワンショットモードではスマートアルゴリズムが使えない。** フィルタのカットオフ周波数が
  保証できないため（AN5867 p18）。在室検出を使うなら連続モードしかない。
- **ゲイン低減モード（wide mode）を有効にするとスマートアルゴリズムが使えない**（AN5867 p18）。

**電気的な現実**:

- VDD 1.7〜3.6 V（DS p5）。**5 V は掛けられない。** M5Stack Unit TMOS PIR の Grove 赤線は
  5 V なので、ユニット基板側で降圧している。
- ロジックレベルは VDD 比例（`VIL` ≤ 0.3×VDD、`VIH` ≥ 0.7×VDD、DS p6）。
- 消費電流は設定次第で大きく変わる。1 Hz・平均 32 で 5 µA、パワーダウンで 1.5 µA、
  30 Hz・平均 32 だと 115.65 µA（DS p25, Table 21）。
- **電源投入から 2.5 ms でブート完了**し、そのあと**自動でパワーダウンモードに入る**
  （AN5867 p10）。つまり電源を入れただけでは測っていない。
- 割り込みピンは push-pull かオープンドレインを `CTRL3` の `PP_OD` で選べる。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x5A` **固定**（SAD = `1011010`、DS p11） |
| アドレス変更 | 不可 |
| 電源 | 1.7〜3.6 V |
| インタフェース | I2C または 3 線 SPI。`CS` ピンで選ぶ |
| レジスタアドレス | 8bit。**オートインクリメントは常時有効**（DS p11） |
| バイト順 | 16bit 値は **LSB first**（`_L` が先、アドレスが小さい方が下位）、2 の補数 |
| 識別 | `0x0F`（WHO_AM_I）が `0xD3` |
| ODR | 0.25〜30 Hz、8 段。既定はパワーダウン（`CTRL1` = `0x00`） |
| 視野 | 80°（full FOV、赤外強度が 50 % になる角度） |
| 感度 | `TAMBIENT` 100 LSB/℃、`TOBJECT` 2000 LSB/℃ |
| 環境温度精度 | ±0.3 ℃（-10〜60 ℃）、±0.6 ℃（-40〜85 ℃） |
| 赤外波長 | 5〜20 µm |
| 消費電流 | 1.5 µA（パワーダウン）〜115.65 µA（30 Hz、平均 32） |
| ブート時間 | 2.5 ms。完了後は自動でパワーダウン |
| 割り込み | INT ピン 1 本。DRDY か 3 フラグの OR。極性・出力形式とも設定可 |
| 搭載製品 | M5Stack Unit TMOS PIR (U185) — `0x5A` |

## レジスタ・コマンド

### メインページ

全体は DS p20 の Table 15。**Reserved を書くと恒久的に壊れる**と明記されている。

| アドレス | 名前 | R/W | 既定値 | 意味 |
|---|---|---|---|---|
| `0x0C` | LPF1 | R/W | `0x04` | `LPF_P_M[2:0]`（動体+在室用）、`LPF_M[2:0]`（動体用） |
| `0x0D` | LPF2 | R/W | `0x22` | `LPF_P[2:0]`（在室用）、`LPF_A_T[2:0]`（温度急変用） |
| `0x0F` | WHO_AM_I | R | `0xD3` | 固定の識別値 |
| `0x10` | AVG_TRIM | R/W | `0x03` | `AVG_T[1:0]`（環境温度）、`AVG_TMOS[2:0]`（物体温度） |
| `0x17` | CTRL0 | R/W | `0xF1` | `GAIN[2:0]`。`000` = wide mode、`111` = 既定ゲイン |
| `0x1D` | SENS_DATA | R/W | 工場書込 | 感度。`感度 = 値(符号付き) × 16 + 2048` |
| `0x20` | CTRL1 | R/W | `0x00` | `BDU`(b4)、`ODR[3:0]` |
| `0x21` | CTRL2 | R/W | `0x00` | `BOOT`(b7)、`FUNC_CFG_ACCESS`(b4)、`ONE_SHOT`(b0) |
| `0x22` | CTRL3 | R/W | `0x00` | `INT_H_L`、`PP_OD`、`INT_MSK[2:0]`、`INT_LATCHED`、`IEN[1:0]` |
| `0x23` | STATUS | R | `0x00` | b2 = `DRDY`。**`FUNC_STATUS` を読むと 0 に戻る** |
| `0x25` | FUNC_STATUS | R | `0x00` | b2 = `PRES_FLAG`、b1 = `MOT_FLAG`、b0 = `TAMB_SHOCK_FLAG` |
| `0x26`/`0x27` | TOBJECT_L/H | R | — | 物体温度、16bit、2 の補数 |
| `0x28`/`0x29` | TAMBIENT_L/H | R | — | 環境温度、16bit、2 の補数、100 LSB/℃ |
| `0x38`/`0x39` | TOBJ_COMP_L/H | R | — | 環境温度補償後の物体温度 |
| `0x3A`/`0x3B` | TPRESENCE_L/H | R | — | 在室アルゴリズムの生の値 |
| `0x3C`/`0x3D` | TMOTION_L/H | R | — | 動体アルゴリズムの生の値 |
| `0x3E`/`0x3F` | TAMB_SHOCK_L/H | R | — | 温度急変アルゴリズムの生の値 |

`0x00`〜`0x0B`、`0x0E`、`0x11`〜`0x16`、`0x18`〜`0x1C`、`0x1E`/`0x1F`、`0x24` は **Reserved**。

**ODR[3:0]**（`CTRL1`、DS p25, Table 20）: `0000` = パワーダウン、`0001` = 0.25 Hz、
`0010` = 0.5、`0011` = 1、`0100` = 2、`0101` = 4、`0110` = 8、`0111` = 15、`1xxx` = 30 Hz。

**IEN[1:0]**（`CTRL3`、DS p27, Table 22）: `00` = INT ピンを Hi-Z、`01` = DRDY を出す、
`10` = 3 フラグの OR（`INT_OR`）を出す。`10` のときは `INT_LATCHED` を 0 にしなければならない。

### 埋め込み機能ページ（閾値の設定）

在室・動体の閾値やヒステリシスは**メインページには無い**。別ページに置かれていて、
`CTRL2` の `FUNC_CFG_ACCESS`（b4）を 1 にしてから、`FUNC_CFG_ADDR`（`0x08`）・
`FUNC_CFG_DATA`（`0x09`）・`PAGE_RW`（`0x11`）を使って間接的に読み書きする（AN5867 p4）。

| アドレス | 名前 | 既定値 |
|---|---|---|
| `0x20`-`0x21` | PRESENCE_THS | `0xC8`（200 LSB） |
| `0x22`-`0x23` | MOTION_THS | `0xC8` |
| `0x24`-`0x25` | TAMB_SHOCK_THS | `0x0A`（10 LSB） |
| `0x26` | HYST_MOTION | `0x32`（50 LSB） |
| `0x27` | HYST_PRESENCE | `0x32` |
| `0x28` | ALGO_CONFIG | `0x00` |
| `0x29` | HYST_TAMB_SHOCK | `0x02` |
| `0x2A` | RESET_ALGO | `0x00` |

**転送の形**: レジスタアドレス（SUB）1 バイトを書き、読むときは repeated START を打って
必要な長さを要求する。**オートインクリメントは常時有効**なので、`0x26` を指定して 4 バイト
読めば `TOBJECT` と `TAMBIENT` が一度に取れる（DS p11-12, Table 10〜13）。

## 典型的な使い方

1. `0x0F`（WHO_AM_I）を読む。`0xD3` が返れば STHS34PF80。
2. **視野を空にする。** 以降の手順は視野内に熱源が無い状態で行う（AN5867 p18）。
3. `0x10`（AVG_TRIM）に平均化の設定を書く。AN5867 の例は `0x02`（環境温度 8 平均、
   物体温度 32 平均、AN5867 p10）。
4. `0x20`（CTRL1）に ODR を書く。AN5867 の例は `0x07`（15 Hz）。**ここから測定が始まる。**
   ノイズ対策として `BDU`（b4）も立てておく。
5. 読み出しループ:
   1. `0x23`（STATUS）を読む。`DRDY`（b2）が 0 なら待つ。
   2. **`0x25`（FUNC_STATUS）を読む。** これで `DRDY` が 0 に戻り、同時に
      在室・動体・温度急変のフラグが手に入る。
   3. `0x26` から必要なバイトを読む（`TOBJECT` なら 2 バイト、`TAMBIENT` まで含めるなら 4 バイト）。
   4. 1 に戻る。

   この順序は AN5867 p10 の手順書きそのままである。
6. 閾値を変えるなら、**いったんパワーダウンに戻し**（`CTRL1` の ODR を `0000`）、
   埋め込み機能ページで値を書き換え、**アルゴリズムリセット手順を実行してから**
   連続モードに戻す（AN5867 p22, Section 7.4）。

## 利用上の注意

- **`FUNC_STATUS`（`0x25`）を読むことが `DRDY` のクリアである。** ここが一番の落とし穴。
  - `STATUS`（`0x23`）の `DRDY` は **常にラッチモード**で、**`FUNC_STATUS` を読むまで
    落ちない**（AN5867 p10 の Note）。`STATUS` を何度読んでも消えない。
  - INT ピンをラッチモード（`INT_LATCHED` = 1）で使っている場合も、
    **ピンが解放されるのは `FUNC_STATUS` を読んだとき**である（AN5867 p10）。
  - したがって「`STATUS` を読む → データを読む」だけのループを書くと、**`DRDY` が
    立ちっぱなしになり、同じサンプルを何度も読み続ける**。`FUNC_STATUS` を読む一手が要る。
  - **2 か所から `FUNC_STATUS` を読むと片方が `DRDY` を取りこぼす。** 読む場所は 1 か所に決める。

- **一方で、`FUNC_STATUS` のフラグ自体はラッチではない。** ここは直感に反する。
  `PRES_FLAG` / `MOT_FLAG` / `TAMB_SHOCK_FLAG` は **ODR の周期ごとに、条件が真なら 1、
  偽に戻れば 0 になる**（DS p27、AN5867 p18 の両方が同じことを書いている）。
  読んだからクリアされるのではない。
  - つまり **ポーリング間隔が ODR より長いと、短い動体イベントを丸ごと見落とす**。
    「読み取りクリアだから読めば取りこぼさない」という前提は、このチップには当てはまらない。
  - 短いイベントを確実に捕まえたいなら、INT ピンを `IEN[1:0]` = `10`（`INT_OR`）で使い、
    ハード側でエッジを拾う。
  - なお `INT_OR` を使うときは `INT_LATCHED` を 0 にしなければならない（DS p26）。
    そのため INT ピン側もラッチされず、パルスとして出る。

- **ヒステリシスがあるので、閾値をまたぐ瞬間の挙動は非対称。** `PRES_FLAG` は
  `TPRESENCE > PRESENCE_THS` で 1 になり、`TPRESENCE < PRESENCE_THS - HYST_PRESENCE`
  で 0 に戻る（AN5867 p19-21）。既定値は閾値 200 LSB、ヒステリシス 50 LSB。

- **電源を入れただけでは測っていない。** ブート完了（2.5 ms）後は自動でパワーダウンに
  入る（AN5867 p10）。`CTRL1` の ODR が `0000` のままだと、`TOBJECT` を読んでも
  値は更新されない。「読めているのに値が動かない」の原因はほぼこれ。

- **視野内に人が居る状態で立ち上げてはいけない。** パワーダウンから連続モードに移る瞬間と、
  アルゴリズムリセットの瞬間は、視野が空でなければならない（AN5867 p18）。
  そうでないと、そこに居る人が「背景」として学習されてしまい、以後検出されなくなる。
  ODR を変えるときも同じ手順を踏む必要がある（AN5867 の Section 3.3）。

- **閾値を書き換えたらアルゴリズムをリセットしないと反映されない。** 埋め込み機能ページの
  値や `LPF1`/`LPF2` を変えた場合、AN5867 p22 の手順（パワーダウン → 設定変更 →
  `FUNC_CFG_ACCESS` = 1 → `PAGE_RW` の `FUNC_CFG_WRITE` = 1 → …）を踏む必要がある。
  書いただけでは効かない。

- **`FUNC_CFG_ACCESS` を 1 にしている間はメインページが読めない。** DS p26 の脚注が
  「このビットが 1 のときメインページのレジスタは読み書きできない。戻すには 0 を書く」と
  明記している。**戻し忘れると、以後どのレジスタを読んでも意味の無い値が返る。**

- **`CTRL0`（ゲインモード）はブート/リブートで既定値に戻る。** wide mode が必要な用途では、
  電源投入のたびに設定し直さなければならない（DS p6）。しかも wide mode では
  スマートアルゴリズムが使えない（AN5867 p18）。

- **Reserved レジスタに書くと恒久的に壊れる。** DS p20 の Table 15 が
  "Writing to those registers may cause permanent damage to the device" と明記している。
  レジスタ全域をダンプするような probe を書くときは、**読みだけに留める**。

- **`BDU` を立てないと 16bit 値が壊れる。** データレディ信号と同期しない読み方をする場合、
  `CTRL1` の `BDU`（b4）を 1 にすることが強く推奨されている（AN5867 p11）。
  立てておくと、`_L` を読んでから `_H` を読むまでそのペアの更新が止まる。
  `TOBJECT` と `TAMBIENT` は別々に管理される。

- **`0x5A` は本カタログ内で `mlx90614`（赤外温度計）および `gp8413`（A2=L, A1=H, A0=L の
  とき）と衝突する。** STHS34PF80 のアドレスは変えられないので、同じバスには置けない。
  なお `mlx90614` の方は EEPROM でアドレスを変更できる。

- **平均化サンプル数はノイズと消費電流のトレードオフ。** `AVG_TMOS` を 2048 にすると
  ノイズは 10 LSBrms まで下がるが、30 Hz では設定自体が表から消える（DS p25, Table 21 の
  空欄）。高 ODR と高平均は両立しない。

- **`TOBJECT` を温度として解釈しない。** 2000 LSB/℃ という感度は、視野を放射率 99 % 超の
  黒体で完全に覆った条件でのものである（DS p5 の脚注 2）。実際の場面では距離と占有率で
  値が変わる。人の在室判定に使うなら、生の値ではなくアルゴリズムのフラグを使う。

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| レジスタ一覧（アドレス・既定値・Reserved） | datasheet p20（Table 15） |
| 埋め込み機能ページのレジスタ一覧 | datasheet p21（Table 16）、AN5867 p4（Table 3） |
| 各レジスタのビット定義 | datasheet p22-29（Section 10） |
| `DRDY` と `FUNC_STATUS` の関係 | **AN5867 p10（Section 4.2, 4.3 と Note）**、datasheet p27 |
| `FUNC_STATUS` のフラグがラッチでないこと | datasheet p27（Section 10.11）、AN5867 p18 |
| 起動シーケンスと推奨初期設定 | AN5867 p10（Section 4.1） |
| `BDU` の意味 | AN5867 p11（Section 4.4） |
| 埋め込み機能ページへのアクセス手順 | AN5867 p4（Section 2.1、2.1.1、2.1.2） |
| アルゴリズムのリセット手順 | AN5867 p22（Section 7.4） |
| 在室 / 動体 / 温度急変の判定アルゴリズム | AN5867 p19-21（Section 7.1〜7.3） |
| ODR と消費電流の表 | datasheet p25（Table 20、Table 21） |
| 平均化サンプル数とノイズ | datasheet p23（Table 18、Table 19） |
| I2C の転送形式とスレーブアドレス | datasheet p11-12（Section 5.1） |
| 電源電圧・視野・感度・精度 | datasheet p5-6（Table 2〜Table 5） |
| ゲインモードと動作温度範囲の関係 | datasheet p6（Table 5） |
| 機械可読なレジスタマップ | [`profiles/sths34pf80.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/sths34pf80.yaml) |
| アドレスと搭載製品 | [`chips/sths34pf80.yaml`](../../chips/sths34pf80.yaml)、[`products/m5stack-u185.yaml`](../../products/m5stack-u185.yaml) |
| 本リポジトリの probe が何を触るか | [`scenarios/sths34pf80/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/sths34pf80/p0.yaml)、`collection/sketches/sths34pf80__characterize/sths34pf80__characterize.ino` |
| ミラーされた datasheet | [M5Stack OSS の STHS34PF80_Datasheet.pdf](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/UNIT-TMOS%20PIR/STHS34PF80_Datasheet.pdf) |
| ミラーされた AN5867 | [M5Stack OSS の STHS34PF80_Application_note.pdf](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/UNIT-TMOS%20PIR/STHS34PF80_Application_note.pdf) |
| ユニットの仕様・検出距離 | [M5Stack Unit TMOS PIR 製品ページ](https://docs.m5stack.com/en/unit/UNIT-TMOS%20PIR) |
| 実測（あれば） | `observations/sths34pf80/`（現時点で存在しない） |

## 出典と確度

- **一次資料 1**: STMicroelectronics *STHS34PF80 datasheet*、**DS13916 Rev 2、全 40 ページ**。
  `chips/sths34pf80.yaml` が記録している版（revision `2`、2023-07-05）と一致する。
- **一次資料 2**: STMicroelectronics *AN5867 — STHS34PF80: how to use*、
  **Rev 2、全 27 ページ**。
- `chips/sths34pf80.yaml` が記録している `https://www.st.com/resource/en/datasheet/sths34pf80.pdf`
  は **2026-08-15 時点で取得できなかった**（`tools/datasheet_text.py` も curl も TLS 層で
  切断される）。代わりに M5Stack Unit TMOS PIR の製品ページからたどれる
  `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/UNIT-TMOS%20PIR/STHS34PF80_Datasheet.pdf`
  と同 `..._Application_note.pdf` を取得し、pypdf でテキスト化して読んだ。
  **datasheet の版はカタログの記録（Rev 2）と一致しており、食い違いは無い。**
  本文中のページ番号はこれらの PDF のもの。DS と AN5867 のどちらを指すかは都度明記した。
- レジスタのアドレス・ビット定義・既定値、`DRDY` のクリア規則、`FUNC_STATUS` のフラグが
  レベル追従であること、ODR と消費電流、視野・感度・精度、I2C の転送形式は、
  すべて上記 2 文書の記述。
- **「`FUNC_STATUS` は在室・動体フラグをラッチし、読むとクリアされる」という理解は
  誤りである。** これは広く流布しているが、DS p27 と AN5867 p18 はどちらも
  「ODR の周期ごとに条件が真なら 1、偽になれば 0 に戻る」と書いている。
  読み取りでクリアされるのは `STATUS` の `DRDY`（と、ラッチモードの INT ピン）であって、
  `FUNC_STATUS` の 3 フラグではない。本ガイドは datasheet の記述に従った。
- M5Stack Unit TMOS PIR が 5 V を降圧している、という記述は、チップの VDD 上限（3.6 V）と
  Grove の 5 V が両立しないことからの**推論**である。ユニットの回路図で実際の
  レギュレータを確認してはいない。**未確認**。
- `0x5A` の衝突（`mlx90614`、`gp8413`）は `chips/*.yaml` のアドレス欄を突き合わせて確認した。
- **未確認**: 実機のキャプチャが無い。WHO_AM_I が `0xD3` を返すか、リセット直後の各レジスタが
  表のとおりか、400 kHz で動くかは確かめていない。`scenarios/sths34pf80/p0.yaml` は
  これらを read-only で確認する計画だが、まだ走っていない。
- **未調査**: 書き込み側（ODR 設定、ワンショット、埋め込み機能ページ、閾値設定）は
  probe が read-only のため一切触れていない。在室検出が実際にどの距離・どの条件で
  効くかも未検証。
