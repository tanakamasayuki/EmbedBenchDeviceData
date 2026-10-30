# SCD41 — 光音響式 NDIR CO2 センサ（単発測定に対応した SCD4x）

> **本物の CO2 濃度**を光音響方式で測るセンサ。温度と湿度も返すが、それは CO2 補正のための
> 内蔵センサであって、温湿度計としての精度は無い。SCD40 の兄弟で、違いは単発測定の有無。
>
> **確度**: Sensirion の datasheet（SCD4x, Version 1.7, 2025-04。SCD40 / SCD41 / SCD43 共通）に基づく。
> **実測は無い**。`observations/scd41/` は存在せず、
> [`profiles/scd41.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/scd41.yaml) の `evidence` も
> `source: datasheet` / `confidence: tentative` である。

## これは何か

Sensirion PASens®（光音響）方式の NDIR CO2 センサ。密閉されたセルに赤外線を当て、CO2 分子が
吸収した熱で生じる圧力変化（音）をマイクで拾う。従来の NDIR が必要とした長い光路が不要になり、
10.1 × 10.1 × 6.5 mm に収まっている。**eCO2（等価値）ではなく、CO2 の濃度そのものを測る。**

CO2 の補正のために温度・湿度センサを内蔵しており、その値も `read_measurement` で一緒に返る。
ただし **温度 ±0.8 ℃、湿度 ±6 %RH（typ, 15〜35 ℃）** で、SHT30 / SHT40 とは桁が違う。
「ついでに読める温湿度」として扱い、環境計測に使うなら別のセンサを載せる。

**SCD40 との違い**（同じ datasheet、同じアドレス `0x62`、基本コマンドも同じ）:

| | SCD40 | SCD41 |
|---|---|---|
| 単発測定 | **無い**（周期測定のみ） | `measure_single_shot` `0x219D`（5 s）、`measure_single_shot_rht_only` `0x2196`（50 ms） |
| power down / wake up | 無い | `0x36E0` / `0x36F6` |
| ASC 期間パラメータ | 無い | `0x2445` / `0x2340` / `0x244E` / `0x234B` |
| 精度保証範囲 | 400〜2000 ppm で ±(50 ppm + 5%) | 400〜1000 ppm で ±(50 ppm + 2.5%)、1001〜2000 で ±(50 ppm + 3%)、2001〜5000 で ±(40 ppm + 5%) |
| 平均消費電流 | 周期測定 15 mA @3.3V | 同左。ただし単発 5 分間隔なら **0.45 mA** |

**バス上で見分ける方法**: `get_sensor_variant` `0x202F` を読み、返ってきた word の bits[15:12] が
`0000` なら SCD40、`0001` なら SCD41、`0101` なら SCD43。アドレスもコマンドの大半も共通なので、
**presence だけでは区別できない**。

できること:

- 周期測定（5 秒更新）、低消費電力周期測定（約 30 秒更新）、単発測定（SCD41 のみ）。
- 温度オフセット、設置高度、外気圧による補正値の設定。
- 強制再校正（FRC）と自動自己校正（ASC）。
- 設定の EEPROM への保存、シリアル番号の読み出し、自己診断、工場出荷リセット。
- 電源を落とさずスリープに入れる（`power_down` / `wake_up`、SCD41 のみ）。

できないこと・持っていないもの:

- **5 秒より速く測れない。** 周期測定の更新間隔は 5 秒固定。単発測定でも最短間隔は 5 秒。
- **データレディの割り込みピンが無い。** `get_data_ready_status` `0xE4B8` を叩くか、
  `read_measurement` の NACK を見る。
- **測定中はほとんどのコマンドが通らない。** 通るのは `read_measurement` /
  `get_data_ready_status` / `stop_periodic_measurement` / `set_ambient_pressure` /
  `get_ambient_pressure` の 5 つだけ。
- **測定値は 1 更新につき 1 回しか読めない。** 読むとバッファが空になる。
- **I2C アドレスを変える手段が無い**（`0x62` 固定、選択ピンも無い）。
- **校正を自分で検証できない。** 基準ガスか屋外の外気が要る。
- 応答時間は τ63% で 60 秒（CO2）。息を吹きかけた瞬間の追従を期待するものではない。

電気的な現実:

- VDD 2.4〜5.5 V。ただし **ピーク電流が 175 mA（3.3 V）/ 115 mA（5 V）**。
  平均は周期測定で 15 mA（3.3 V）/ 11 mA（5 V）。
- **無負荷時の電源リップルが 30 mV を超えてはいけない**（内部レギュレーションと干渉する）。
  datasheet はセンサ専用の LDO を推奨している。細い Grove ケーブルの先で他の負荷と電源を
  共有すると、ピーク時に電圧が落ちてセンサがリセットする。
- VDD と VDDH は同電位で、**センサの近くで**接続する。
- 動作範囲は −10〜60 ℃ / 0〜95 %RH（結露不可）。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x62` 固定。**変更手段なし** |
| 電源 | 2.4〜5.5 V（typ 3.3 V または 5.0 V） |
| ピーク電流 | 175 mA @3.3V / 115 mA @5V |
| I2C 速度 | 0〜400 kHz（standard / fast）（**未実測**） |
| コマンド長 | 16bit、MSB first。**コマンド自体には CRC が付かない** |
| 応答フレーム | 16bit ワード + CRC 1 バイトの 3 バイト単位 |
| CRC | CRC-8、多項式 `0x31`、初期値 `0xFF`、反転なし、最終 XOR `0x00` |
| 出力 | CO2 [ppm] → 温度 → 湿度 の 3 ワード = **9 バイト** |
| CO2 出力範囲 | 0〜40000 ppm（精度保証は上表のとおり） |
| 更新間隔 | 周期 5 s / 低消費電力周期 約 30 s / 単発 最短 5 s |
| 温湿度精度（typ） | 温度 ±0.8 ℃、湿度 ±6 %RH（15〜35 ℃） |
| 応答時間 | CO2 τ63% = 60 s、湿度 90 s、温度 120 s |
| 起動時間 | 電源投入後 最大 30 ms、reinit 後 最大 30 ms |
| 割り込み | 無し（ポーリング） |
| EEPROM | `persist_settings` は 2000 回保証 |

## コマンド

すべて 16bit、MSB first。**コマンドワードには CRC が付かない**（SHT3x / SGP30 とは違い、
3bit の埋め込み CRC も無い）。パラメータを送るときは、パラメータのワードにだけ CRC を付ける。

[`profiles/scd41.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/scd41.yaml) が持っているのは下表の ★ の 7 つで、
残りは datasheet Table 9（p.8）にある。

| コマンド | 種別 | 実行時間 | 測定中 | 意味 |
|---|---|---|---|---|
| ★ `0x21B1` | send | — | 不可 | `start_periodic_measurement`（5 s 更新） |
| ★ `0xEC05` | read | 1 ms | 可 | `read_measurement`。CO2 / 温度 / 湿度の 9 バイト |
| ★ `0x3F86` | send | **500 ms** | 可 | `stop_periodic_measurement` |
| ★ `0x3682` | read | 1 ms | 不可 | `get_serial_number`（48bit = 3 ワード） |
| ★ `0x3639` | read | **10000 ms** | 不可 | `perform_self_test`。`0` なら異常なし |
| ★ `0x3646` | send | 30 ms | 不可 | `reinit`（EEPROM から設定を再読み込み） |
| ★ `0x219D` | send | **5000 ms** | 不可 | `measure_single_shot`（**SCD41 / SCD43 のみ**） |
| `0x2196` | send | 50 ms | 不可 | `measure_single_shot_rht_only`（CO2 は 0 ppm で返る） |
| `0x241D` / `0x2318` | write / read | 1 ms | 不可 | 温度オフセットの設定 / 読み出し（既定 4 ℃） |
| `0x2427` / `0x2322` | write / read | 1 ms | 不可 | 設置高度の設定 / 読み出し |
| `0xE000` | write / read | 1 ms | **可** | 外気圧の設定 / 読み出し |
| `0x362F` | send+fetch | **400 ms** | 不可 | `perform_forced_recalibration`（FRC） |
| `0x2416` / `0x2313` | write / read | 1 ms | 不可 | ASC の有効 / 無効（既定は有効） |
| `0x243A` / `0x233F` | write / read | 1 ms | 不可 | ASC のターゲット濃度（既定 400 ppm） |
| `0x21AC` | send | — | 不可 | `start_low_power_periodic_measurement`（約 30 s 更新） |
| `0xE4B8` | read | 1 ms | **可** | `get_data_ready_status` |
| `0x3615` | send | **800 ms** | 不可 | `persist_settings`（EEPROM 書き込み） |
| `0x3632` | send | **1200 ms** | 不可 | `perform_factory_reset` |
| `0x202F` | read | 1 ms | 不可 | `get_sensor_variant`（SCD40 / 41 / 43 の判別） |
| `0x36E0` / `0x36F6` | send | 1 / 30 ms | 不可 | `power_down` / `wake_up`（**SCD41 / SCD43 のみ**） |
| `0x2445` / `0x2340` | write / read | 1 ms | 不可 | ASC initial period（既定 44 時間） |
| `0x244E` / `0x234B` | write / read | 1 ms | 不可 | ASC standard period（既定 156 時間） |

**応答フレームの形（Sensirion 共通）**: 応答は「16bit ワード（MSB first）+ CRC-8 1 バイト」の
3 バイト単位で並ぶ。`read_measurement` なら
`[CO2 上位, CO2 下位, CRC, 温度上位, 温度下位, CRC, 湿度上位, 湿度下位, CRC]` の 9 バイト。
CRC は**直前の 2 バイトだけ**が対象（多項式 `0x31` = x⁸+x⁵+x⁴+1、初期値 `0xFF`、
入出力ビット反転なし、最終 XOR `0x00`。検算値: CRC(`0xBEEF`) = `0x92`。datasheet p.23 に
C の実装例そのものが載っている）。書き込み方向では CRC が必須。読み出し方向では master が
読み飛ばしてよい。

**CRC が落ちたときと、値がおかしいときは別の話である。** CRC 不一致は「バスの上でバイトが壊れた」
という意味で、疑うのはプルアップ・配線・速度・電源のリップルであってセンサではない。CRC が通って
いるのに値が変なら、センサは本当にその値を返している（暖機中、ASC がずれている、温度オフセットが
未設定、まだ 5 秒経っていない、など）。**バイト境界が 1 つずれると全ワードの CRC が落ちる**ので、
全滅するときはフレーム長の解釈を先に疑う。

**値の変換**（datasheet 3.6.2、p.9）:

- CO2 [ppm] = word[0]（そのまま符号なし 16bit）
- 温度 ℃ = −45 + 175 × word[1] / (2¹⁶ − 1)
- 湿度 %RH = 100 × word[2] / (2¹⁶ − 1)

温度・湿度の式は SHT3x と同一である。

## 典型的な使い方

周期測定（既定の使い方）:

1. 電源投入から 30 ms 待つ。
2. 状態が不明なら `0x3F86`（stop_periodic）を送り、**500 ms 待つ**。前回の run が周期測定のまま
   終わっている可能性があるため。
3. `0x3682`（シリアル番号）を読んで存在確認、`0x202F` で SCD40 / SCD41 の別を確認する。
4. 必要なら温度オフセット `0x241D`、設置高度 `0x2427` を設定し、恒久化するなら `0x3615`
   （800 ms）を送る。**この段階は idle でしかできない。**
5. `0x21B1`（start_periodic）を送る。
6. 5 秒ごとに `0xE4B8`（data ready）を読み、下位 11bit が 0 でなければ `0xEC05` で 9 バイト読む。
   `0xE4B8` を使わずに `0xEC05` を直接叩き、NACK なら「まだ」と判断してもよい。
7. 気圧の実測値があるなら `0xE000` に書く（**これは測定中でも通る数少ないコマンドの 1 つ**）。
8. 止めるときは `0x3F86` を送り、500 ms 待ってから次の操作に移る。

単発測定（SCD41 のみ、低消費電力用途）:

1. `power_down` していたなら `0x36F6`（wake_up）を送り、30 ms 待つ。
   **wake_up は ACK を返さない。** 応答が無いのが正常で、復帰の確認はシリアル番号の読み出しで行う。
2. `0x219D`（measure_single_shot）を送り、**5000 ms 待つ**。
3. `0xEC05` で 9 バイト読む。
4. 必要なら `0x36E0`（power_down）でスリープへ。
5. 繰り返す。**間隔は 5 秒以上**。ASC は 5 分間隔を前提に最適化されている。

## 利用上の注意

- **強制再校正（FRC）はベンチから取り消せない。** 手順は (1) 実運用と同じモード・同じ電圧で
  3 分以上（単発なら 1 分間隔で 3 回以上）均一・一定の CO2 環境に置く、(2) `0x3F86` を送って
  500 ms 待つ、(3) `0x362F` に基準濃度 [ppm] + CRC を書き、400 ms 待って補正量を読む
  （返り値 − `0x8000` が ppm 単位の補正量、`0xFFFF` は「事前に運転していない」ための失敗）。
  **「元の校正に戻す」コマンドは存在しない。** 手が届くのは `0x3632`（perform_factory_reset）
  だけで、これは EEPROM の全設定に加えて **FRC と ASC の履歴をまとめて消す**。直前の状態への
  復帰ではなく、積み上げた校正履歴の破棄である。したがって、
  **信頼できる基準（校正済みの CO2 計、あるいは屋外の外気）を用意できないなら FRC を打たない。**
  誤った基準値を書き込めば、その誤差がそのまま残る。
- **ASC は既定で有効で、「週に一度は 400 ppm の外気に当たる」ことを仮定している。** 正確には、
  周期測定・低消費電力周期測定・単発 5 分間隔のいずれかで累積 4 時間以上動かした状態を 1 週間
  続けるあいだに、3 分以上 400 ppm の空気に晒される、という前提である。締め切った室内に
  置きっぱなしの機器ではこの仮定が崩れ、基準が室内の最低濃度に引っ張られる。
  常時屋内なら `0x2416` に `0` を書いて ASC を切り（`0x3615` で恒久化）、代わりに FRC を
  運用するか、そもそも精度を割り切る。
- **設定は既定で RAM にしか無い。** 温度オフセット、設置高度、ASC の有効/無効は
  `0x3615`（persist_settings、800 ms）を送らないと電源で消える。ただし **EEPROM は 2000 回保証**
  なので、毎起動で書くコードを書かない。なお FRC / ASC の校正履歴は別の EEPROM に自動保存される。
- **測定中に通るコマンドは 5 つだけ。** `read_measurement` / `get_data_ready_status` /
  `stop_periodic_measurement` / `set_ambient_pressure` / `get_ambient_pressure`。それ以外を
  送りたければ先に `0x3F86`。
- **`0x3F86` の直後 500 ms は応答しない。** ここで次のコマンドを送ると失敗する。
- **`wake_up`（`0x36F6`）は ACK を返さない。** datasheet が明記している（3.11.4）。
  I2C の書き込みが「アドレス NACK」で返るのが正常で、これを故障と誤解しない。
- **`read_measurement` の NACK は「データがまだ無い」の合図。** 通信エラーではない。
- **測定値は 1 更新につき 1 回。** 読むとバッファが空になる。2 つの処理から読むと片方が取りこぼす。
- **温度オフセットの既定値は 4 ℃。** ケース内の自己発熱を見込んだ値なので、**実装した状態で
  熱平衡に達してから決め直さないと、温湿度が系統的にずれる**。推奨範囲は 0〜20 ℃。
  なお温度オフセットは CO2 の精度には影響しない。
- **`perform_self_test`（`0x3639`）は 10 秒ブロックする。** 起動シーケンスに軽い気持ちで
  入れると、起動が 10 秒遅くなる。
- **電源が弱いとリセットする。** ピーク 175 mA（3.3 V）。5 V 給電のほうがピーク電流は小さい
  （115 mA）。無負荷リップルは 30 mV 以下。
- **応答が遅い。** CO2 の τ63% は 60 秒。「息を吹きかけて値が跳ねるか」で動作確認するなら、
  数十秒待つこと。数秒で戻ることを期待しない。
- **温湿度を環境計測に使わない。** ±0.8 ℃ / ±6 %RH。ENV シリーズの数字と混同しない。
- **400 ppm 未満の環境に晒すと ASC 有効時の精度に影響する**（datasheet 脚注 5）。
- **`0x62` に応答があっても SCD40 か SCD41 かは分からない。** `0x202F` を読むこと。
  SCD40 用のコードで SCD41 を叩いても大半は動いてしまうので、単発測定を使うつもりなら
  variant の確認は必須。

## どこに何が書いてあるか

datasheet は [SCD4x Datasheet Version 1.7](https://sensirion.com/media/documents/48C4B7FB/67FE0194/CD_DS_SCD4x_Datasheet_D1.pdf)（2025-04、30 ページ。SCD40 / SCD41 / SCD43 共通）。

| 知りたいこと | 参照先 |
|---|---|
| **CO2 の精度（SCD40 と SCD41 の差）・応答時間・経年ドリフト** | 1.1 節 Table 1、p.3 |
| 湿度・温度の精度 | 1.2 / 1.3 節 Table 2・Table 3、p.3 |
| **電源電圧・ピーク電流・平均電流（モード別）** | 2.1 節 Table 4、p.4 |
| 絶対最大定格・動作範囲・寿命 | 2.2 節 Table 5、p.4 |
| **ピン配置・LDO 推奨・リップル 30 mV の制約** | 2.3 節 Table 6 と本文、p.5 |
| 起動時間・SCL 周波数 | 2.4 節 Table 7、p.6 |
| I2C アドレス `0x62` | 3.2 節 Table 8、p.7 |
| ワードと CRC の並び | 3.3 節、p.7 |
| **4 つのコマンドシーケンス型（write / send / read / send+fetch）** | 3.4 節 Figure 2、p.7 |
| **コマンド一覧・実行時間・測定中に使えるか** | 3.5 節 Table 9、p.8 |
| start / read / stop と 500 ms の待ち | 3.6 節、p.9–10 |
| **生値 → ppm / ℃ / %RH の変換式と実例** | 3.6.2 節 Table 11、p.9 |
| 温度オフセット（既定 4 ℃）・高度・気圧補正 | 3.7 節、p.10–12 |
| **FRC の手順・返り値の意味・`0xFFFF` の失敗** | 3.8.1 節 Table 19、p.13 |
| **ASC の前提（週 1 回 400 ppm、累積 4 時間）** | 3.8 節冒頭、p.13。ターゲット濃度は 3.8.4 / 3.8.5、p.14–15 |
| 低消費電力周期測定・data ready の判定 | 3.9 節、p.15–16 |
| persist_settings（EEPROM 2000 回） | 3.10.1 節 Table 26、p.16 |
| シリアル番号 | 3.10.2 節 Table 27、p.17 |
| 自己診断（10 秒） | 3.10.3 節 Table 28、p.17 |
| 工場出荷リセット（FRC / ASC 履歴を消す） | 3.10.4 節 Table 29、p.17 |
| reinit | 3.10.5 節 Table 30、p.18 |
| **`get_sensor_variant`（SCD40 / 41 / 43 の判別ビット）** | 3.10.6 節 Table 31、p.18 |
| **単発測定モード（SCD41 / SCD43 のみ）の全体像** | 3.11 節、p.19 |
| measure_single_shot / rht_only | 3.11.1 / 3.11.2 節 Table 32・Table 33、p.19–20 |
| **power_down / wake_up（wake_up は ACK しない）** | 3.11.3 / 3.11.4 節 Table 34・Table 35、p.20 |
| ASC の initial / standard period | 3.11.5〜3.11.8 節、p.21–22 |
| **CRC-8 のパラメータと C の実装例** | 3.12 節 Table 40、p.23 |
| 機械可読なコマンド定義 | [`profiles/scd41.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/scd41.yaml) |
| チップの素性・アドレス・datasheet リンク | [`chips/scd41.yaml`](../../chips/scd41.yaml) |
| 兄弟チップ | [`chips/scd40.yaml`](../../chips/scd40.yaml) / [`profiles/scd40.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/scd40.yaml) |
| 製品（Unit CO2L） | [`products/m5stack-u104.yaml`](../../products/m5stack-u104.yaml) |
| 収集シナリオ（何を測る / 何を見送る） | [`scenarios/scd41/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/scd41/p0.yaml) |
| 実測用 probe のソース（未実行） | `collection/sketches/scd41__characterize/scd41__characterize.ino` |
| 比較用ライブラリ | [`libraries/sensirion-arduino-i2c-scd4x.yaml`](../../libraries/sensirion-arduino-i2c-scd4x.yaml) |

## 出典と確度

- コマンド、実行時間、フレーム、CRC、変換式、FRC / ASC の仕様、電気仕様: **SCD4x Datasheet
  Version 1.7（2025-04）**。
- M5Stack が配布しているのは **Version 1.1（2021-04、23 ページ）** で、`get_sensor_variant`
  （`0x202F`）や ASC のターゲット濃度・期間パラメータが載っていない。古い版を見ていると
  「SCD40 と SCD41 をバス上で区別する方法が無い」と誤解する。
- **未確認（実測なし）**: `observations/scd41/` は存在しない。`0x62` の応答、シリアル番号、
  variant の値、9 バイトのフレーム、CRC、単発測定の実際の所要時間、400 kHz での動作、
  いずれも確かめられていない。
- **未調査**: [`scenarios/scd41/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/scd41/p0.yaml) では自己診断、
  強制再校正、周期測定、低消費電力単発、設定の恒久化が `deferred` になっている。
  とくに FRC は取り消しが効かないため、実測シナリオに入れる場合は個体を割り切る前提が要る。
- **本ガイドの SCD40 との比較表**は datasheet 3.11 節の「SCD41 & SCD43 only」表記と
  Table 1 / Table 4 の記載から起こしたもので、**実機 2 種を並べて確かめてはいない**。
