# Unit CardKB (U035) — カード型 QWERTY キーボードユニット（初代）

> [Unit CardKB v1.1 (U035-B)](m5stack-mcu-u035-b.md) の 1 世代前。**プロトコルは v1.1 の
> 出荷時ファームウェアと同じ**で、ポインタバイトを持たない 1 バイト読み出しに
> 押されたキーの ASCII が返るだけ。レジスタは無い。
>
> **確度**: 本機には v1.1 のような User Manual PDF が無く、`chips/` が指す一次資料は
> 製品ページだけである。**ただし本機は、出荷時スレーブ firmware のソースが
> ベンダーから公開されている唯一の CardKB である。** 1 バイト読みという
> プロトコルの形と、レジスタが存在しないことは、そのソースから直接読み取れる。
> **それでもバス上で確認された挙動は一つも無い。**

## これは何か

88 × 54 × 5mm のカード型 50 キー QWERTY キーボード。マトリクススキャンを基板上の
ATmega が行い、その結果を I2C で返す。ホストから見える相手は ATmega であって
キーマトリクスではない。**つまり通信仕様はこの製品固有のもの**である。

**製品としての詳しい説明・キーコード表・修飾キー（Shift / Sym / Fn）のワンショットと
ダブルクリックロックの挙動・差し替え firmware のレジスタマップは、
[`m5stack-mcu-u035-b.md`](m5stack-mcu-u035-b.md) に書いてある。重複させないので、
そちらを先に読むこと。** 本ガイドは初代に固有の差分だけを扱う。

本カタログには CardKB 系が 3 つある。

| 世代 | key | SKU | MCU | アドレス | 一次資料 | `profiles/` |
|---|---|---|---|---|---|---|
| **初代（本機）** | `m5stack-mcu-u035` | U035 | 下記の食い違いあり | `0x5f` | 製品ページのみ | 無し |
| v1.1 | `m5stack-mcu-u035-b` | U035-B | ATmega8A | `0x5f` | [User Manual PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/806/Unit_CardKB_v1_1_User_Manual_EN.pdf) | [あり](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u035-b.yaml) |
| CardKB2 | `m5stack-mcu-u215` | U215 | ESP32-C61HF4 | `0x5f` | [User Manual PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1225/Unit_CardKB2_User_Manual_EN.pdf) | [あり](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u215.yaml) |

### 初代と v1.1 の違い

**I2C プロトコルの違いは見つかっていない。** どちらも `0x5f`、どちらもポインタバイト無しの
1 バイト読み、どちらも 50 キー（通常キー 47 + 修飾キー 3）、どちらも RGB LED 1 個。
分かっている違いは次の 4 点である。

#### 1. ハードウェアタイプの値が違う

差し替え firmware を書き込んだ場合にだけ現れるレジスタ `0xFD`（ハードウェアタイプ）が、
**初代は `0x01`、v1.1 は `0x11`** を返す。`M5Unit-KEYBOARD` の
`src/unit/unit_CardKB.hpp` にそのまま書かれている。

```
constexpr uint8_t TYPE_CARDKB{0x01};      //!< SKU:U035
constexpr uint8_t TYPE_CARDKB_V11{0x11};  //!< V11 SKU:U035-B
```

**つまり初代と v1.1 は、差し替え firmware を書き込みさえすれば I2C から区別できる。**
出荷時のままではどちらも区別がつかない（バージョンレジスタが無いため）。

#### 2. 重量が違う

初代 **17.0g**、v1.1 14.3g。寸法はどちらも 88.0 × 54.0 × 5.0mm で同一。
**外形が同じで重さだけ違うので、手元の個体を秤にかけるのが最も手軽な見分け方**になる。

#### 3. 一次資料の量が違う

v1.1 には I2C の節と全キーコード表を含む User Manual PDF があるが、
**初代には無い。** [`chips/m5stack-mcu-u035.yaml`](../../chips/m5stack-mcu-u035.yaml) の
`datasheets` は製品ページ 1 件（`kind: product-page`）だけで、v1.1 の
`kind: manual` とは資料の質が違う。初代のキーコードを知りたい場合は、
**v1.1 の User Manual のキーコード表を流用することになる**（同一である保証は無い）。

#### 4. MCU の型番が資料によって食い違う（未解決）

これは本機で最も注意すべき点である。

| 資料 | 記載 |
|---|---|
| [`chips/m5stack-mcu-u035.yaml`](../../chips/m5stack-mcu-u035.yaml) | ATmega8A |
| 製品ページ（ISP Download Interface の節） | ATMega8A |
| ベンダー公開 firmware のディレクトリ名 | `Unit/CARDKB/firmware_328p/` — **328p** と名乗っている |
| `M5Unit-KEYBOARD` の firmware スケッチ README（書き込み時のボード設定） | U035 = **ATmega328**、U035-B = ATmega8 |

**前 2 者と後 2 者が真っ向から食い違っている。** 書き込み時のボード設定を間違えると
fuse や clock の設定を誤って**個体を文鎮化させ得る**ので、これは実害のある食い違いである。
**差し替え firmware を書き込む前に、必ず基板上の刻印を目視で確認すること。**

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x5f`（固定。変更手段なし） |
| I2C 速度 | **不明。** v1.1 の User Manual は 100kHz と明記するが、**初代にはその記述が無い**。100kHz と考えるのが無難 |
| 電源 | 5V（Grove の赤） |
| インタフェース | Grove HY2.0-4P |
| 中身 | **ATmega328P か ATmega8A（資料が食い違う。上記参照）** + M5Stack ファームウェア + RGB LED ×1 |
| キー数 | 50（通常キー 47 + 修飾キー 3） |
| 割り込み | 無し（ポーリング） |
| 出荷時プロトコル | ポインタ無しの 1 バイト読み。キーの ASCII、無ければ `0x00` |
| ハードウェアタイプ | `0x01`（差し替え firmware を書き込んだ場合のみ読める） |
| 寸法 / 重量 | 88.0 × 54.0 × 5.0mm / **17.0g** |

## レジスタ / コマンド

**出荷時のままではレジスタが存在しない。** これは v1.1 と同じだが、**初代については
その根拠が推測ではなく、ベンダーが公開しているスレーブ側 firmware のソースそのもの**である。

`M5-ProductExampleCodes` の `Unit/CARDKB/firmware_328p/CardKeyBoard/CardKeyBoard.ino` は、

- `Wire.begin(0x5f);` でスレーブアドレス `0x5f` を登録し、
- `requestEvent()` で **`Wire.write(KeyMap[KEY - 1][Mode]);` すなわち 1 バイトだけ**書き、
- **`Wire.onReceive()` を一切登録していない**（= ホストからの書き込みを受ける口が無い）。

つまり **`0x5f` に対する書き込みトランザクションは、firmware 側に受け手が存在しない。**
ポインタバイトを書いても捨てられるだけである。v1.1 のガイドが User Manual を根拠に
述べている「出荷時プロトコルはこれがすべて」は、初代についてはソースで裏が取れている。

差し替え firmware（`M5Unit-KEYBOARD` 同梱スケッチ）を書き込んだ場合のレジスタ
`0x10` / `0x20` / `0xFD` / `0xFE` は初代・v1.1 で共通である。
**表は [`m5stack-mcu-u035-b.md` のレジスタの節](m5stack-mcu-u035-b.md#レジスタ差し替え-firmware-を書き込んだ場合のみ)にある。
唯一の違いは `0xFD` が返す値（初代 `0x01`）だけ。**

**何があればさらに確定するか**（有用な順）:

1. **実機のキャプチャ。** 1 バイト読みで何が返るか、押下時か離上時か、ポインタバイトを
   書いた場合に ACK するか。1 回で決着する。
2. **基板上の MCU 刻印の確認。** 上記 4 の食い違いは目視 1 回で終わる。
   **本機について最も費用対効果の高い作業がこれである。**
3. **出荷個体の flash 吸い出し。** 公開ソースが出荷バイナリと同一かの確認。

## 典型的な使い方

[v1.1 のガイドの「出荷時のまま使う」](m5stack-mcu-u035-b.md#出荷時のまま使う)と同じ。
一定周期で 1 バイト読み、`0x00` なら何も無し、それ以外は ASCII として扱う。

**差し替え firmware で使う場合も v1.1 と同じ手順**だが、`0xFD` が返すのは `0x11` ではなく
**`0x01`** である。`M5Unit-KEYBOARD` の `begin()` は `TYPE_CARDKB`（`0x01`）と
`TYPE_CARDKB_V11`（`0x11`）の両方を受け付けるので、ライブラリはそのまま使える。

## 利用上の注意

**v1.1 と共通の落とし穴（1 バイトバッファの取りこぼし、`begin()` が最初の 1 バイトを
捨てる理由、ポインタバイトの使い捨て、スキャン結果が 1 周期前のバッファであること、
修飾ビットの正規化、`0x20` の書き込みが状態をリセットすること等）は
[v1.1 のガイドの「利用上の注意」](m5stack-mcu-u035-b.md#利用上の注意)にある。そちらを読むこと。**
以下は初代に固有のものだけを書く。

- **差し替え firmware を書き込む前に、必ず基板上の MCU 刻印を読むこと。**
  `chips/` と製品ページは ATmega8A と言い、ベンダーの firmware ディレクトリ名と
  `M5Unit-KEYBOARD` の README は ATmega328 と言う。**ボード設定を誤ると
  fuse を書き損じて個体を失う。** 資料を信じずに現物を見る。
- **firmware の差し替えは戻せない。** 出荷時 firmware のバイナリは配布されていない。
  ただし**本機については出荷時 firmware の「ソース」が公開されている**ので、
  ATmega に合わせてビルドし直せば理屈の上では復元できる。v1.1 には無い、初代だけの逃げ道である。
  （実際にビルドが通るか、出荷バイナリと同等になるかは**未確認**。）
- **出荷時のままでは初代と v1.1 を I2C から区別できない。** どちらも `0x5f` で
  1 バイト読みしか応答しない。**確実な見分け方は重量（17.0g / 14.3g）か基板の刻印。**
- **キーコード表は v1.1 のものを流用するしかない。** 初代には User Manual が無い。
  **両者のキーコードが一致するという確認は取れていない。**
- **I2C 速度が不明。** v1.1 の User Manual は 100kHz を明記するが、初代の資料には
  レートの記述が無い。**100kHz から始めること。**
- **本カタログ内の `0x5f` 衝突**（`0x5f` は本カタログで最も混み合うアドレスの一つ）:
  - [`chips/m5stack-mcu-u035-b.yaml`](../../chips/m5stack-mcu-u035-b.yaml)（CardKB v1.1）と
    [`chips/m5stack-mcu-u215.yaml`](../../chips/m5stack-mcu-u215.yaml)（CardKB2）が同じ `0x5f`。
    **CardKB は 3 世代すべて `0x5f` で、どれもアドレスを変更できない。同じバスに 2 台並べられない。**
  - [`chips/gp8413.yaml`](../../chips/gp8413.yaml) — GP8413 DAC の `0x5f`
    （A2=A1=A0 すべて high の設定）。**アドレス設定次第で衝突する。** DAC 側は
    `0x58`〜`0x5f` の 8 通りから選べるので、**回避するなら DAC 側をずらす。**

  回避手段は DAC 側のアドレス変更か、I2C マルチプレクサ
  （[`chips/tca9548a.yaml`](../../chips/tca9548a.yaml) / [`chips/pca9548a.yaml`](../../chips/pca9548a.yaml)）である。
- **`0x5f` が ACK しても CardKB とは限らない。** 出荷時はバージョンレジスタが無いので、
  返る 1 バイトだけでは CardKB2 とも GP8413 とも区別がつかない。しかも
  **押されているキーの ASCII が「たまたま」もっともらしい値に見えることがある。**
- **本ガイドに書かれたことは、寸法・重量を除いてどれも実測で確認されていない。**

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| 製品仕様・寸法・重量（ベンダー公式） | [M5Stack 製品ページ](https://docs.m5stack.com/en/unit/cardkb) |
| **出荷時スレーブ firmware のソース**（レジスタが無いことの根拠） | [`M5-ProductExampleCodes` の `Unit/CARDKB/firmware_328p/CardKeyBoard/CardKeyBoard.ino`](https://github.com/m5stack/M5-ProductExampleCodes/blob/master/Unit/CARDKB/firmware_328p/CardKeyBoard/CardKeyBoard.ino) |
| キーコード表・修飾キーの挙動・出荷時プロトコルの説明 | **v1.1 の資料を流用する。** [Unit CardKB v1.1 User Manual](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/806/Unit_CardKB_v1_1_User_Manual_EN.pdf) と [`m5stack-mcu-u035-b.md`](m5stack-mcu-u035-b.md) |
| ハードウェアタイプ `0x01` の定義 | `M5Unit-KEYBOARD` v0.1.0 の `src/unit/unit_CardKB.hpp`（`TYPE_CARDKB`） |
| 差し替え firmware のレジスタ表・書き込み時のボード設定 | `M5Unit-KEYBOARD` v0.1.0 の `examples/firmware/CardKB_Firmware/README.md` |
| ユニットの構成・搭載 IC | [`chips/m5stack-mcu-u035.yaml`](../../chips/m5stack-mcu-u035.yaml) |
| 製品（SKU・ベンダー URL） | [`products/m5stack-u035.yaml`](../../products/m5stack-u035.yaml) |
| 機械可読なレジスタマップ | **無い。** `profiles/m5stack-mcu-u035.yaml` は存在しない（v1.1 の [`profiles/m5stack-mcu-u035-b.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u035-b.yaml) が最も近い） |
| 取得シナリオ・probe スケッチ | **無い。** `scenarios/m5stack-mcu-u035/` も `collection/sketches/m5stack-mcu-u035__*` も存在しない（v1.1 には [`collection/sketches/m5stack-mcu-u035-b__characterize/`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/sketches/m5stack-mcu-u035-b__characterize) がある） |
| 実測 | **無い。** `observations/m5stack-mcu-u035/` は存在しない |
| 次世代のガイド（本文の大部分はこちら） | [`m5stack-mcu-u035-b.md`](m5stack-mcu-u035-b.md) |
| CardKB2 のガイド | [`m5stack-mcu-u215.md`](m5stack-mcu-u215.md) |

## 出典と確度

- **datasheet: 無い。** v1.1 の User Manual PDF に相当するものが初代には存在しない。
  [`chips/m5stack-mcu-u035.yaml`](../../chips/m5stack-mcu-u035.yaml) の `datasheets` は
  製品ページ 1 件（`kind: product-page`）のみ。
- **専用のベンダー Arduino ライブラリ: 無い。** ただし **`M5Unit-KEYBOARD` が
  ハードウェアタイプ `0x01` として本機を明示的に認識する**（`src/unit/unit_CardKB.hpp`）。
  6 機種の中で、一般公開ライブラリに機種として名前が載っている数少ない例である。
  なお `M5Unit-KEYBOARD` は Arduino Library Manager 経由のライブラリで、
  [`collection/vendor_libraries.toml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor_libraries.toml) には載らない。
- **`profiles/`: 無い。キャプチャ: 無い。**
- アドレス `0x5f`、1 バイト読み、レジスタが存在しないこと、書き込みの受け手が無いこと:
  **ベンダー公開の出荷時 firmware ソース `CardKeyBoard.ino` から直接読み取った**
  （`Wire.begin(0x5f)` / `requestEvent()` の 1 バイト書き / `onReceive` 不在）。
  **本ガイドで最も確度の高い部分。** ただし公開ソースが出荷バイナリと同一である保証は無い。
- ハードウェアタイプ `0x01`: `M5Unit-KEYBOARD` v0.1.0 のソースに基づく。
- キーコード表・修飾キーの挙動・差し替え firmware のレジスタ: **v1.1 の資料からの流用であり、
  初代で確認されたものではない。**
- MCU 型番: **未解決の食い違いがある**（上記「初代と v1.1 の違い」4 を参照）。
  本ガイドはどちらかを正としていない。
- 「押されたキー」か「離されたキー」か: v1.1 のガイドが User Manual とライブラリの
  記述の食い違いとして未決着としている点。**初代の firmware ソースを読む限り、
  キー検出後に離されるまで待つ処理が入っており「離上時に値が確定する」ように見えるが、
  これはソースの読みであって実測ではない。** 初代の firmware から v1.1 の挙動を
  結論してはいけない（別のバイナリである）。
- **次にやるべき最も有用な一手**: **基板上の MCU 刻印を目視で確認すること。**
  資料が真っ向から食い違っており、しかも誤ると個体を壊す種類の情報であるため、
  キャプチャよりも先にこれをやる価値がある。刻印を確認したうえで、
  v1.1 用の [`collection/sketches/m5stack-mcu-u035-b__characterize/`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/sketches/m5stack-mcu-u035-b__characterize) を
  流用した read-only probe を当てれば、1 バイト読みの挙動も同時に確定する。
