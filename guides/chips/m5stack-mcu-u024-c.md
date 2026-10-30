# Unit Joystick v1.1 (U024-C) — アナログジョイスティック入力ユニット（第 2 世代）

> 初代 Unit Joystick (U024) の MCU を ATmega328P から ATmega8A に載せ替えた版。
> **アドレスもプロトコルも初代と同じ**で、ポインタバイト無しの 3 バイト読み出しに
> X, Y, ボタンが返る。**レジスタは存在しない。**
>
> **確度**: datasheet も I2C プロトコル文書も専用 Arduino ライブラリも無い。本リポジトリに
> `profiles/m5stack-mcu-u024-c.yaml` は存在せず、レジスタマップも実測も持っていない。
> 根拠は M5Stack 製品ページの仕様表と、**初代と共通の**ホスト側サンプルコードだけである。
> **バス上で確認された挙動は一つも無い。**

## これは何か

アナログ 2 軸ジョイスティックと押し込みスイッチを、基板上の ATmega8A が AD 変換して
I2C で返すユニット。ホストから見える相手は ATmega であってスティックではない。
**つまり I2C の相手はファームウェアであり、通信仕様はこの製品固有のもの**である。

本カタログには Unit Joystick が **3 世代**ある。本機はその真ん中にあたる。

| 世代 | key | SKU | MCU | アドレス | レジスタマップ |
|---|---|---|---|---|---|
| 初代 | `m5stack-mcu-u024` | U024 | ATmega328P | `0x52` | 無し |
| **v1.1（本機）** | `m5stack-mcu-u024-c` | U024-C | **ATmega8A** | `0x52` | 無し |
| Joystick2 | `m5stack-mcu-u024-v2` | U024-V2 | STM32G030 | `0x63` | [あり](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u024-v2.yaml) |

**初代との違いとして公開されているのは MCU の型番だけ**である。製品ページの仕様表
（寸法 48.0 × 24.0 × 32.0mm、重量 11.0g、X/Y 出力 0-255、Z ボタン出力 0/1、
LEGO 互換穴 ×2、I2C `0x52`）は初代のものと一字一句同じで、Protocol 表も同じ 1 行である。
**v1.1 で挙動が変わったという記述はベンダーのどこにも無い。** ATmega328P → ATmega8A は
コストダウン方向の載せ替えで、Flash は 32KB → 8KB、SRAM は 2KB → 1KB に減る。
機能が増える方向の変更ではない。

できること・できないことは初代とまったく同じであるため、
**[初代のガイド](m5stack-mcu-u024.md)を参照すること。** 以下は本機に固有の点だけを書く。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x52`（固定。変更手段なし。**初代と同一**） |
| 電源 | 5V（Grove の赤） |
| インタフェース | Grove HY2.0-4P |
| I2C 速度 | ベンダーのサンプルは 400kHz 初期化。上限は**未実測**。**ATmega8A は初代の 328P より遅い可能性がある**（下記注意） |
| 中身 | **ATmega8A**（製品ページの表記は「MEGA8A」）+ アナログジョイスティック + 押し込みスイッチ |
| 割り込み | 無し（ポーリング） |
| 転送の形 | **ポインタバイト無し**の 3 バイト読み出し |
| X / Y 出力 | 0〜255（サンプルコードのコメントは実効レンジを 10〜250 と書く） |
| ボタン出力 | `0` = 離、`1` = 押（サンプルコードのコメント由来。**未確認**） |
| 寸法 / 重量 | 48.0 × 24.0 × 32.0mm / 11.0g |
| その他 | LEGO 互換穴 ×2 |

## レジスタ / コマンド

**既知のレジスタマップは無い。そもそもレジスタが無い可能性が高い。**

製品ページの「Protocol」表は初代と同一の 1 行である。

| REG | len | description | return values |
|---|---|---|---|
| `0x52` | 3 | Read joystick status | `[0]` X VALUE `[1]` Y VALUE `[2]` BTN STATUS |

**`REG` 欄の `0x52` はレジスタ番号ではなく I2C アドレスである。** M5Stack の製品ページは
レジスタを持たないユニットでもこの表形式を使い回している。ベンダーのサンプルコードは
`Wire.requestFrom(0x52, 3)` を呼ぶだけで、**ポインタバイトを一切書いていない**。

**何があればこれが確定するか**（有用な順）:

1. **実機のキャプチャ。** ポインタバイトの有無と要求長を振った read-only probe を当てれば決着する。
   **初代と本機を並べて同じ probe を当てれば、「v1.1 で挙動が変わっていない」という
   本ガイドの前提そのものを検証できる。** これは初代単体を測るより価値が高い。
2. **スレーブ側 firmware のソース。** CardKB と Makey は `M5-ProductExampleCodes` に
   `firmware_328p/` が公開されているが、**Joystick には firmware ディレクトリが無い。**
   仮に公開されても `firmware_328p` 相当のものは初代用であり、ATmega8A の v1.1 用は
   さらに別物になる点に注意。
3. **UIFlow が実機を叩くところのキャプチャ。**
4. **ATmega8A の flash 吸い出し。**

**Joystick2（`0x63`）のレジスタマップを本機に当てはめないこと。** あれは STM32G030 上の
まったく別のファームウェアである。

## 典型的な使い方

[初代のガイド](m5stack-mcu-u024.md#典型的な使い方)と同じ。要約すると、
Wire を開いて `0x52` に 3 バイト要求し、X / Y / ボタンとして解釈する。起動直後に
スティックを触らずに読んだ値を中心として記録しておく。

## 利用上の注意

- **初代と外形も仕様表も同一なので、手元の個体がどちらか分からなくなりやすい。**
  I2C から機種を同定する手段は無い（バージョンレジスタもハードウェア種別レジスタも無い）。
  **見分けるには基板上の MCU の刻印を読むしかない**（ATMEGA328P か ATMEGA8A か）。
  CardKB が `0xFD` のハードウェアタイプで U035 と U035-B を区別できるのとは対照的である。
- **本カタログ内の `0x52` 衝突**: [`chips/m5stack-mcu-u024.yaml`](../../chips/m5stack-mcu-u024.yaml)
  （初代）が**同じ `0x52`** である。どちらもアドレスを変更できないので、
  **初代と v1.1 を同じバスに並べることはできない。** 2 台使いたい場合は
  Joystick2（`0x63`）を混ぜるか、I2C マルチプレクサ
  （[`chips/tca9548a.yaml`](../../chips/tca9548a.yaml) / [`chips/pca9548a.yaml`](../../chips/pca9548a.yaml)）を挟む。
- **VL53L0X との取り違えに注意。** VL53L0X の 7bit アドレスは `0x29` で、
  **その 8bit 表記の write アドレスが `0x52`** である（[`chips/vl53l0x.yaml`](../../chips/vl53l0x.yaml) の
  notes 参照）。バス上では衝突しないが、ログや資料の突き合わせで混乱する。
- **ATmega8A は初代の ATmega328P より非力である。** 8MHz 内蔵発振で動いている場合、
  400kHz の I2C にスレーブとして追従できず clock stretch が長くなる、あるいは
  取りこぼす可能性がある。**ベンダーのサンプルが 400kHz で `Wire.begin()` しているのは
  初代向けに書かれたコードで、v1.1 で検証された値ではない。** 通信が不安定なら
  まず 100kHz に落として切り分けること。**これは未確認の推測であり、実測で確かめる価値が高い。**
- **ボタンの極性が M5Stack の他ユニットと逆に見える。** サンプルコードのコメントは
  `0: released 1: pressed`。**未確認。**
- **X / Y を 0 や 255 で判定しない。** サンプルのコメントは実効レンジ 10〜250。
- **割り込みが無いのでポーリング周期が応答性の上限になる。**
- **本ガイドに書かれたことは、寸法・重量・MCU 型番を除いてどれも実測で確認されていない。**
  加えて、**「初代と挙動が同じ」という前提自体がベンダーの明言ではなく、
  仕様表が同一であることからの推定である。**

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| 製品仕様・寸法・Protocol 表・MCU 型番（ベンダー公式） | [M5Stack 製品ページ](https://docs.m5stack.com/en/unit/joystick_1.1) |
| ホスト側サンプル（初代と共通。専用のものは無い） | [`M5-ProductExampleCodes` の `Unit/JOYSTICK/Arduino/Joystick/Joystick.ino`](https://github.com/m5stack/M5-ProductExampleCodes/blob/master/Unit/JOYSTICK/Arduino/Joystick/Joystick.ino) |
| もう一つのホスト側サンプル | [`M5Stack` ライブラリの `examples/Unit/JOYSTICK/JOYSTICK.ino`](https://github.com/m5stack/M5Stack/blob/master/examples/Unit/JOYSTICK/JOYSTICK.ino) |
| ユニットの構成・搭載 IC | [`chips/m5stack-mcu-u024-c.yaml`](../../chips/m5stack-mcu-u024-c.yaml) |
| 製品（SKU・ベンダー URL） | [`products/m5stack-u024-c.yaml`](../../products/m5stack-u024-c.yaml) |
| 機械可読なレジスタマップ | **無い。** `profiles/m5stack-mcu-u024-c.yaml` は存在しない |
| 取得シナリオ・probe スケッチ | **無い。** `scenarios/m5stack-mcu-u024-c/` も `collection/sketches/m5stack-mcu-u024-c__*` も存在しない |
| 実測 | **無い。** `observations/m5stack-mcu-u024-c/` は存在しない |
| 前世代（同アドレス。用途と注意点はほぼ共通） | [ガイド](m5stack-mcu-u024.md) / [`chips/m5stack-mcu-u024.yaml`](../../chips/m5stack-mcu-u024.yaml) |
| 後継機（Joystick2、`0x63`、レジスタあり） | [ガイド](m5stack-mcu-u024-v2.md) / [`profiles/m5stack-mcu-u024-v2.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u024-v2.yaml) |

## 出典と確度

- **datasheet: 無い。** M5Stack はこの世代についても I2C プロトコル文書を公開していない。
  [`chips/m5stack-mcu-u024-c.yaml`](../../chips/m5stack-mcu-u024-c.yaml) の `datasheets` は
  製品ページ 1 件のみで `kind: product-page` である。
- **ベンダー Arduino ライブラリ: 無い。** `M5Unit-Joystick` に相当するリポジトリは存在せず、
  [`collection/vendor_libraries.toml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor_libraries.toml) にも
  `collection/vendor/` にもエントリが無い。`M5Unit-Joystick2` は後継機用で、
  アドレスも MCU もレジスタも違うため**本機には使えない。**
- **`profiles/`: 無い。キャプチャ: 無い。**
- MCU が ATmega8A であること: **製品ページの仕様表に「Main MCU: MEGA8A」と明記されている**
  （初代の製品ページには MCU 行が無い）。これが v1.1 に固有の、確かな情報の全部である。
- アドレス `0x52`、3 バイト読み、バイトの並び: **製品ページの Protocol 表に基づく**。
  ただしその表は初代のものと同一であり、v1.1 の実機で検証された形跡は無い。
- 400kHz、実効レンジ 10〜250、ボタン極性: **初代向けサンプルコードの引数とコメント由来**。
  本機で確かめられていない。
- **次にやるべき最も有用な一手**: 初代（U024）と本機（U024-C）を**並べて**同じ read-only probe を
  当て、応答を突き合わせること。この 2 機種の唯一の実質的な問いは
  「MCU が変わってプロトコルまで変わっていないか」であり、それは 1 回の比較キャプチャで
  決着する。あわせて 100kHz と 400kHz の両方で測れば、ATmega8A の速度制約という
  もう一つの未確認事項も同時に潰せる。
