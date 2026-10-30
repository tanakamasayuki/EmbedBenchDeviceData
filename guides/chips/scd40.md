# SCD40 — 光音響式 CO2 センサ

> Sensirion の CO2 センサ。NDIR ではなく光音響（photoacoustic）方式で、CO2 と一緒に温度・
> 湿度も返す。16bit のコマンドを送り、16bit ワードごとに CRC-8 が付いた応答を読む。
> 測定周期は 5 秒。
>
> **確度**: Sensirion の SCD4x datasheet Version 1.7（2025-04）に基づく。
> `profiles/scd40.yaml` の evidence は `datasheet` / `tentative` で、**実機のキャプチャは
> まだ無い**。CRC が実機で一致するか、5 秒周期の実際のばらつき、400kHz での動作は未確認。
>
> **強制校正（FRC, `0x362F`）は保存された CO2 の基準値を書き換え、ベンチからは元に戻せない。**
> 詳細は「利用上の注意」を参照。

## これは何か

光音響方式の CO2 センサ。密閉したセルの中の CO2 に赤外線を当て、CO2 が吸収して起こす
圧力波をマイクで拾う。NDIR（長い光路が必要）に比べて小さく作れるのが売りで、SCD4x は
LGA パッケージ 1 個（約 0.6g）に収まっている。上面の白い保護膜は剥がしてはいけない。

**CO2 だけでなく温度と湿度も返す。** `read_measurement`（`0xEC05`）の 1 回の読み出しで、
CO2[ppm]・温度[℃]・湿度[%RH] の 3 ワードが順に取れる。湿度と温度はセルの補償にも使われている。

**基本の使い方は 5 秒周期の periodic measurement** で、`start_periodic_measurement` を送って
おけば 5 秒ごとに値が更新される。30 秒周期の低消費モードもある。

長期の精度は 2 つの校正機能で保つ。

- **ASC（自動自己校正）**: 既定で有効。「測定を続けているうちに、週に 1 回は 400ppm の
  外気に 3 分以上さらされる」という前提で、観測履歴の下限を 400ppm に合わせ込む。
- **FRC（強制校正）**: 外部で得た CO2 の基準値を与えて、その場で合わせ込む。

### できないこと・持っていないもの

- **SCD40 には単発測定コマンドが無い。** `measure_single_shot`（`0x219D`）は
  **SCD41 と SCD43 だけ**の機能である。「5 秒待たずに 1 回だけ測って寝る」ができないので、
  電池駆動で間欠動作させたいなら 30 秒周期の低消費モードを使うか、SCD41 を選ぶ。
  これが本カタログで SCD40 と SCD41 を別のチップとして登録している理由でもある。
- **I2C アドレスは `0x62` 固定。** 変更手段が無い。同じバスに 2 台以上並べられないので、
  複数台には mux が要る。
- **応答が速くない。** CO2 の τ63% が 60 秒、湿度 90 秒、温度 120 秒。5 秒周期は
  「読める周期」であって「追従できる周期」ではない。
- **周期測定中は 5 つのコマンドしか受け付けない。** `read_measurement`、
  `get_data_ready_status`、`stop_periodic_measurement`、`set_ambient_pressure`、
  `get_ambient_pressure` のみ。それ以外を送るには先に停止する。
- **気圧計は内蔵していない。** 気圧補償を効かせるには、外から気圧か標高を書き込む
  （`set_ambient_pressure` / `set_sensor_altitude`）。
- **CO2 の確度が保証されるのは狭い範囲。** 出力レンジは 0〜40000ppm だが、SCD40 の確度が
  規定されているのは 400〜2000ppm で ±(50ppm + 読み値の 5%) のみ。範囲外の値も返ってくるが、
  確度の保証は無い。
- **設定は既定で RAM のみ。** 温度オフセット・標高・ASC の有効/無効は、`persist_settings` を
  送らないと電源を切った時点で消える。

### 電気的な現実

- **ピーク電流が大きい。** 3.3V で typ 175mA / **max 205mA**、5V でも typ 115mA。
  平均は 5 秒周期で 15mA（3.3V）、30 秒周期で 3.2mA。**このピークに耐える電源が要る。**
- **電源リップルは 30mV p-p 以内。** datasheet はセンサ専用の LDO を推奨している。
  「供給電圧は一定に保つこと」とも明記されており、電圧が動くと測定値が動く。
- **VDD と VDDH は必ず同じ電源に、センサの近くで接続する。** VDDH は IR 光源用。
- 電源電圧 2.4〜5.5V（typ 3.3V または 5.0V）。動作温度 -10〜60℃、湿度 0〜95%RH（結露不可）。
- SDA / SCL には外付けプルアップ（10kΩ 程度）が要る。マスタは Low を駆動するだけにする。
- **温度オフセットの既定値は 4℃。** 自己発熱と周辺部品の熱を引くための値で、**筐体に
  組み込んだ後、実運用の条件で熱平衡させてから決め直す**必要がある。ここを放置すると
  温度も湿度も合わない（CO2 の確度には影響しない）。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x62`（**固定、変更不可**） |
| 電源 | 2.4〜5.5V（typ 3.3 / 5.0） |
| ピーク電流 | typ 175mA / **max 205mA**（3.3V）、typ 115mA（5V） |
| 平均電流 | 15mA（3.3V、5 秒周期）、3.2mA（3.3V、30 秒周期） |
| 電源リップル | 30mV p-p 以内 |
| I2C 速度 | 0〜400kHz（standard / fast mode）。未実測 |
| プルアップ | 外付け 10kΩ 程度 |
| コマンド | 16bit、**MSB first**。**コマンドには CRC を付けない** |
| データ | 16bit ワードごとに CRC-8 が 1 バイト付く |
| CRC | 多項式 `0x31`、初期値 `0xFF`、入出力反転なし、最終 XOR なし。`CRC(0xBEEF) = 0x92` |
| 測定周期 | 5 秒（periodic）/ 約 30 秒（低消費 periodic） |
| CO2 出力レンジ | 0〜40000ppm |
| CO2 確度（SCD40） | 400〜2000ppm で ±(50ppm + 読み値の 5%) |
| 温度 | -10〜60℃、±0.8℃（15〜35℃） |
| 湿度 | 0〜100%RH、±6%RH（15〜35℃、20〜65%RH） |
| 応答時間 τ63% | CO2 60 秒 / 湿度 90 秒 / 温度 120 秒 |
| 起動 | ハードリセット後 30ms |
| self-test | **約 10 秒**（`0x3639`） |
| 兄弟品 | **SCD41**（単発測定あり、400〜5000ppm）、SCD43 |

## レジスタ / コマンド

レジスタは無い。16bit のコマンドコードを MSB first で送る。**コマンドコードに CRC は
付けない。** 引数と応答は 16bit ワード単位で、**各ワードの直後に CRC-8 が 1 バイト付く**。
書き込み方向では CRC の送出が必須、読み出し方向で検算するかはホストの自由。

| コマンド | コード | 種別 | 実行時間 | 測定中 |
|---|---|---|---|---|
| `start_periodic_measurement` | `0x21B1` | 送るだけ | — | 不可 |
| `read_measurement` | `0xEC05` | 読み | 1ms | **可** |
| `stop_periodic_measurement` | `0x3F86` | 送るだけ | **500ms** | **可** |
| `set_temperature_offset` | `0x241D` | 書き | 1ms | 不可 |
| `get_temperature_offset` | `0x2318` | 読み | 1ms | 不可 |
| `set_sensor_altitude` | `0x2427` | 書き | 1ms | 不可 |
| `get_sensor_altitude` | `0x2322` | 読み | 1ms | 不可 |
| `set_ambient_pressure` | `0xE000` | 書き | 1ms | **可** |
| `get_ambient_pressure` | `0xE000` | 読み | 1ms | **可** |
| **`perform_forced_recalibration`** | **`0x362F`** | 書いて読む | **400ms** | 不可 |
| `set_automatic_self_calibration_enabled` | `0x2416` | 書き | 1ms | 不可 |
| `get_automatic_self_calibration_enabled` | `0x2313` | 読み | 1ms | 不可 |
| `set_automatic_self_calibration_target` | `0x243A` | 書き | 1ms | 不可 |
| `get_automatic_self_calibration_target` | `0x233F` | 読み | 1ms | 不可 |
| `start_low_power_periodic_measurement` | `0x21AC` | 送るだけ | — | 不可 |
| `get_data_ready_status` | `0xE4B8` | 読み | 1ms | **可** |
| `persist_settings` | `0x3615` | 送るだけ | **800ms** | 不可 |
| `get_serial_number` | `0x3682` | 読み | 1ms | 不可 |
| **`perform_self_test`** | **`0x3639`** | 読み | **10000ms** | 不可 |
| **`perform_factory_reset`** | **`0x3632`** | 送るだけ | **1200ms** | 不可 |
| `reinit` | `0x3646` | 送るだけ | 30ms | 不可 |
| `get_sensor_variant` | `0x202F` | 読み | 1ms | 不可 |
| `measure_single_shot` | `0x219D` | — | — | **SCD40 には無い（SCD41 / SCD43 のみ）** |

`profiles/scd40.yaml` が機械可読な形で持っているのは、このうち
`0x21B1` / `0xEC05` / `0x3F86` / `0x3682` / `0x3639` / `0x3646` の 6 個だけである。

### `read_measurement`（`0xEC05`）の応答 9 バイト

| バイト | 内容 |
|---|---|
| 0-1 | `word[0]` |
| 2 | `word[0]` の CRC |
| 3-4 | `word[1]` |
| 5 | `word[1]` の CRC |
| 6-7 | `word[2]` |
| 8 | `word[2]` の CRC |

```
CO2 [ppm] = word[0]
T   [℃]  = -45 + 175 · word[1] / (2^16 - 1)
RH  [%]  = 100 · word[2] / (2^16 - 1)
```

datasheet の例: `01F4 33 6667 A2 5EB9 3C` → 500ppm, 25℃, 37%RH。

### `get_serial_number`（`0x3682`）の応答 9 バイト

3 ワード（各 CRC 付き）で 48bit のシリアル。`word[0]<<32 | word[1]<<16 | word[2]`。
存在確認と個体識別を兼ねる。

### `get_sensor_variant`（`0x202F`）

`word[0]` の bit15:12 が `0000`=SCD40 / `0001`=SCD41 / `0101`=SCD43。bit11:0 は個体差がある。
**このコマンドは Version 1.7 の datasheet にあり、M5Stack がホストする古いコピー
（Version 1.2, 2022-05）には載っていない。**

## 典型的な使い方

1. 電源投入後、idle になるまで待つ（ハードリセット後 30ms）。
2. 状態が分からないなら `stop_periodic_measurement`（`0x3F86`）を送り、**500ms 待つ**。
   周期測定中は他のコマンドがほとんど通らないので、まずここに落とす。
3. `get_serial_number`（`0x3682`）で 48bit のシリアルを読む。存在確認を兼ねる。
4. 必要なら温度オフセット・標高・気圧を設定する。**電源を切っても残したいなら
   `persist_settings`（`0x3615`、800ms）**を送る。EEPROM は 2000 回保証なので送りすぎない。
5. `start_periodic_measurement`（`0x21B1`）を送る。
6. 5 秒ごとに `read_measurement`（`0xEC05`）を読む。データが無いと NACK が返るので、
   避けたいなら先に `get_data_ready_status`（`0xE4B8`）を読む。
7. 各ワードの CRC を検算する。
8. 止めるときは `stop_periodic_measurement` を送って 500ms 待つ。

## 利用上の注意

- **FRC（`0x362F`）は保存された CO2 基準を書き換え、ベンチから元に戻せない。**
  手順を踏まずに送ると、**そのときの環境の CO2 濃度が「指定した ppm である」ことにされ、
  以後すべての読み値がずれる**。元の校正値を読み出す手段が無いので、書き戻すこともできない。
  復旧に一番近いのは `perform_factory_reset`（`0x3632`）だが、これは EEPROM の設定と
  FRC / ASC の履歴を**全部**消す（工場出荷状態に戻すのであって、直前の状態に戻すのではない）。
  **試し打ちしてよいコマンドではない。** `scenarios/scd40/p0.yaml` でも
  `forced-recalibration` は `deferred` に置かれている。
  - 正しい手順: ①実運用と同じモード・**同じ電源電圧**で、CO2 濃度が均一かつ一定の環境で
    3 分以上動かす（標高 / 気圧の補償値を使うなら先に入れておく）→ ②`stop_periodic_measurement`
    を送って 500ms 待つ → ③`0x362F` に目標 ppm（+CRC）を書き、**400ms 待って**補正量を読む。
  - 応答の `word[0]` が `0xFFFF` なら**失敗**（事前に動かしていない）。成功なら
    補正量[ppm] = `word[0] - 0x8000`。
- **CRC は全データワードに付く。コマンドには付かない。** ここを取り違えると、書き込みが
  一切通らないか、読み出しのバイト境界がずれる。CRC-8、多項式 `0x31`、初期値 `0xFF`、
  入出力の反転なし、最終 XOR なし。実装の確認には `CRC(0xBEEF) = 0x92` が使える。
- **`read_measurement` はバッファを空にする。** 同じ 5 秒の周期内に 2 回読むと、2 回目は
  データが無く **NACK** が返る。**NACK を「通信エラー」と一括りにしない**こと。
  `get_data_ready_status`（`0xE4B8`）を先に読めば NACK を避けられる。
- **周期測定中に送れるのは 5 コマンドだけ。** `read_measurement`、`get_data_ready_status`、
  `stop_periodic_measurement`、`set_ambient_pressure`、`get_ambient_pressure`。
  設定を変えるには必ず一度停止する。
- **`stop_periodic_measurement` の後 500ms は何も受け付けない。** 停止してすぐ次のコマンドを
  送ると失敗する。datasheet が明示的に「500ms 経つまで他のコマンドに応答しない」と書いている。
- **self-test（`0x3639`）は約 10 秒かかる。** その間 I2C は返らない。ホスト側のタイムアウトや
  ウォッチドッグが 10 秒より短いと、正常なセンサが故障に見える。`scenarios/scd40/p0.yaml` でも
  `self-test` は `deferred`。
- **他にも待ち時間の長いコマンドがある。** `perform_factory_reset` 1200ms、
  `persist_settings` 800ms、`stop_periodic_measurement` 500ms、`perform_forced_recalibration`
  400ms、`reinit` 30ms。**実行時間中は次のコマンドを送ってはいけない**（datasheet 3.4）。
- **設定は既定で RAM のみ。** 温度オフセット・標高・ASC の設定は `persist_settings` を
  送らないと電源で消える。逆に送りすぎると EEPROM の寿命（2000 回保証）を削る。
  なお **FRC / ASC の校正履歴は別の EEPROM に自動保存される**ので、`persist_settings` の
  対象ではない。
- **ASC は既定で有効で、「週 1 回は 400ppm の外気に触れる」ことを前提にしている。**
  この前提が成り立たない設置（ずっと室内、常に高濃度）では、勝手に基準がずれていく。
  そういう用途では ASC を切って FRC で合わせるか、ASC のターゲット（`0x243A`）を実情に合わせる。
- **温度オフセットの既定は 4℃。** 筐体に組み込んで熱平衡させた状態で決め直さないと、
  温度も湿度も合わない。
  `T_offset_actual = T_SCD4x - T_reference + T_offset_previous`（推奨範囲 0〜20℃）。
  CO2 の確度には影響しない。
- **ピーク 205mA を供給できる電源が要る。** かつ電源リップルは 30mV 以内、電圧は一定に保つ。
  ハブに何台もぶら下げて電圧が動くと、測定値も動く。FRC を実運用と同じ電圧で行えという
  datasheet の指示も、同じ理由による。
- **アドレス `0x62` は固定で、MCP4725 と衝突する。** 本カタログでは `mcp4725`（MCP4725A1）が
  `0x62` を取り得る。SCD40 側にアドレス変更手段が無いので、衝突したら MCP4725 側で逃げるか
  mux を挟む。複数の SCD4x を並べる場合も同様に mux が要る。
- **SCD41 とは同じアドレス・同じコマンド体系。** バス上で見分けるには
  `get_sensor_variant`（`0x202F`）を読む。ただしこのコマンドは古い版の datasheet には
  載っていない。
- **確度が保証されるのは 400〜2000ppm だけ。** 出力は 0〜40000ppm まで出るが、それは
  「そこまで数字が出る」というだけである。より広い範囲が必要なら SCD41（400〜5000ppm）や
  SCD43（400〜5000ppm、より高精度）を選ぶ。
- **400kHz は datasheet 上サポートされている。** 本カタログでは未実測。

## どこに何が書いてあるか

ページ番号は
[Sensirion SCD4x Datasheet Version 1.7](https://sensirion.com/media/documents/48C4B7FB/67FE0194/CD_DS_SCD4x_Datasheet_D1.pdf)
（全 30 ページ）のもの。**M5Stack がホストする古いコピー（Version 1.2, 2022-05）とは章番号が
違う**ので、ページで引くときは版を確認すること。

| 知りたいこと | 参照先（Version 1.7） |
|---|---|
| CO2 の確度・レンジ・応答時間・年間ドリフト | 1.1（p.3） |
| 湿度・温度の確度と応答時間 | 1.2 / 1.3（p.3） |
| **電源電圧・ピーク電流・平均電流** | 2.1（p.4） |
| 動作温湿度・保管条件・寿命・ESD | 2.2（p.4） |
| ピン配置・VDDH の扱い・プルアップ・LDO 推奨 | 2.3（p.5、Figure 1） |
| SCL 周波数の上限・起動時間・ソフトリセット時間 | 2.4（p.6、Table 7） |
| I2C アドレス（`0x62`） | 3.2（p.7、Table 8） |
| **データ長と CRC の付き方（コマンドには付かない）** | 3.3（p.7） |
| 4 種類のコマンドシーケンスと実行時間の扱い | 3.4（p.7、Figure 2） |
| **コマンド一覧（コード・種別・実行時間・測定中可否）** | 3.5（p.8、Table 9） |
| 基本 3 コマンドと読み値の変換式・NACK の意味 | 3.6（p.9-10、Table 10-12） |
| 温度オフセット・標高・気圧補償 | 3.7（p.10-12） |
| **ASC の前提条件と FRC の正しい手順・失敗判定** | 3.8（p.13-15） |
| 低消費周期モード（30 秒） | 3.9（p.15-16） |
| persist_settings / serial / self-test / factory reset / reinit / variant | 3.10（p.16-18） |
| 単発測定（**SCD41 / SCD43 のみ**）と power_down / wake_up | 3.11（p.19-22） |
| **CRC のパラメータと C の実装例** | 3.12（p.23） |
| パッケージ外形・ランドパターン・実装条件 | 4（p.24-26） |
| 改訂履歴 | 6（p.28） |
| 機械可読なコマンドマップ（6 個のみ） | [`profiles/scd40.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/scd40.yaml) |
| アドレス・datasheet リンク・SCD41 との差の注記 | [`chips/scd40.yaml`](../../chips/scd40.yaml) |
| probe が触る範囲（FRC / self-test は deferred） | [`scenarios/scd40/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/scd40/p0.yaml) |
| 搭載製品 | [`products/m5stack-u103.yaml`](../../products/m5stack-u103.yaml)（Unit CO2） |
| 兄弟品（単発測定あり） | [`chips/scd41.yaml`](../../chips/scd41.yaml)、[`profiles/scd41.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/scd41.yaml) |
| ライブラリ | [`libraries/sensirion-arduino-i2c-scd4x.yaml`](../../libraries/sensirion-arduino-i2c-scd4x.yaml)、[`libraries/m5stack-m5unit-env.yaml`](../../libraries/m5stack-m5unit-env.yaml) |
| 実測（あれば） | `observations/scd40/`（未作成） |

## 出典と確度

- コマンドコード、実行時間、CRC のパラメータ、変換式、電気仕様:
  **Sensirion SCD4x Datasheet Version 1.7（2025-04）**。SCD40 / SCD41 / SCD43 共通の 1 冊で、
  SCD40 固有の差は「確度が 400〜2000ppm 規定」「単発測定コマンドが無い」の 2 点。
- `chips/scd40.yaml` は M5Stack がホストする **Version 1.2（2022-05）** も挙げている。
  こちらは章番号が違い（I2C アドレスの独立した節が無く、3.2 が Data type & length）、
  `get_sensor_variant`・ASC ターゲット・`measure_single_shot_rht_only` が載っていない。
  **ページ番号で引くときはどちらの版か確認すること。**
- `profiles/scd40.yaml` は datasheet 由来だがコマンドを 6 個しか持っておらず、
  `confidence: tentative`。
- **未確認**: 実機のキャプチャがまだ無い。CRC が実機で一致するか、5 秒周期の実際のばらつき、
  データ未準備時の NACK の返り方、シリアル番号の読み出し、400kHz での動作はいずれも未実測。
- **未調査**: self-test、強制校正（FRC）、温度オフセットの書き込み、標高補償、
  `persist_settings` は probe が一切触っていない（`scenarios/scd40/p0.yaml` の `deferred`）。
  FRC については、**副作用が不可逆であるため意図的に外してある**。
- **未確認**: M5Stack Unit CO2（U103）の基板がピーク 205mA をどう供給しているか。Grove の
  5V から直接引いているのか、専用のレギュレータがあるのかは確認していない。
