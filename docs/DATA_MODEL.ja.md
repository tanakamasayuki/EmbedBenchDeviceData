# データモデル

各ファイルの形と規則の正本。`tools/validate.py` がこの文書の規則を検査する。
設計の理由は EmbedBench の `docs/REAL_DEVICE_DATA.ja.md` §5 と `docs/REAL_DEVICE_ROADMAP.ja.md` §1。

`schema_version` はすべて 1。YAML にコメントは書かない（`notes:` に書く）。キーの重複は誤り。

## 0. 原則

- **データはチップ単位で、製品に依存させない。** 製品は部品表。
- **カタログ（chips）を先に広く、振る舞い（knowledge）は後から深く。** knowledge はカタログの項目を繰り返さない。
- **正本は wireskein の記録と分析。** 生のキャプチャは任意（重いものは外に置いて、場所を書く）。
- **時刻を捨てない。** run の中の時刻（probe の時計）は残す。日付は provenance に置く。
- **アドレス NACK の繰り返しを捨てない。** 正規化は読む側（EmbedBench の変換）で行う。
- **逆引きの索引は保存しない。** `tools/build.py` が `build/` に作る。
- **分からないことは `unknown` と書く。** 項目を省くのと区別する。

## 1. キー

| 対象 | キー | 例 |
| --- | --- | --- |
| chip | 型番の slug（ベンダー無し） | `sht30`、`qmp6988`。ファームウェアの載ったマイコンは `m5stack-mcu-u186` |
| product | `slug(vendor)-slug(sku)` | `m5stack-u001-c` |
| library | `slug(owner)-slug(repo)`（repo が owner で始まるなら repo だけ） | `m5stack-m5unit-env` |
| case | `[a-z0-9-]` | `m5unit-env-u001c-basic` |
| run | `<case>__i2c-<kHz>k__<hash16>` | `wire-absent-address__i2c-100k__50f8781ef04f2bec` |

I2CDeviceDB のキーをそのまま引き継いだ。

## 2. カタログ

### chips/<chip>.yaml

| 項目 | 必須 | 内容 |
| --- | --- | --- |
| key、name、manufacturer、tags | ○ | |
| interfaces | ○ | バスごとに 1 つ: `{bus: i2c, addresses: [{addr, note}]}`。`addr` は `0x` ＋ 小文字の 16 進 2 桁で、アドレスピンで届く値をすべて並べる |
| datasheets | ○（空でよい） | `{source: manufacturer / vendor / reseller, kind?, vendor?, url, revision?, date?, note?}`。空なら notes に理由 |
| notes | | |

SPI（`modes`）と UART（`bauds`）の `interfaces` は形の枠だけ決めてある。最初の部品で中身を決める。

### products/<product>.yaml

`key、name、vendor、sku、connector、url、interfaces、components、notes?`。

- `interfaces` は製品の通信の種類を、`i2c, spi, uart, can, pdm, gpio, analog, host, none` の順で並べる。
- `components` は `{chip, bus, addr}`（今は `bus: i2c` だけ）。`addr` はチップの `addresses` のどれか。
- 部品が無い製品は、notes に何なのかを書く（調べて対象外だったことと、まだ調べていないことを分ける）。
- どの製品にも載らないチップは誤り（カタログには載る先が要る）。

### libraries/<library>.yaml、surveys/<survey>.yaml

I2CDeviceDB のまま。library は `repo`（GitHub の URL）からキーが決まり、`supports.chips` に叩くチップを並べる。
survey は `sku_pattern` でラインの境界を決め、`out_of_line`（ラインの外）と `pending`（ラインの中で未登録）を持つ。

## 3. ケース: cases/<case>/case.yaml

| 項目 | 必須 | 内容 |
| --- | --- | --- |
| key | ○ | ディレクトリ名と同じ |
| kind | ○ | `library`（ライブラリの実際の使われ方）か `characterization`（ライブラリを使わず直接叩く） |
| library | library のとき | libraries のキー。characterization には書かない |
| intent | ○ | 何を確かめるか |
| targets | ○ | `[{chip, bus, addr, product?}]`。空なら notes に何を相手にするかを書く |
| phases | | `[{name, intent}]`。`name` は ProbeLog の `## <name>` と同じ文字列 |
| source | | ケースのディレクトリに置いたソースのファイル名（OEP の LibraryTest の形のまま） |
| notes | | |

## 4. 記録: runs/<run-id>/

```text
runs/<run-id>/
  run.json            wireskein-run の出力そのまま（ログ・目印・期待）
  c0001.wireskein     生のキャプチャ。名前は run.json の captures[].file と同じ
  c0001.i2c.json      wireskein の I2C の転送（{"transactions": [...]}）。EmbedBench の skein2trace.py が読む
  provenance.yaml     来歴（下）
```

**run-id** は `tools/run_id.py` が決める。転送の列（`addr、rw、addr_ack、bytes、acks、start_cond、end_cond`。時刻は除く）を
全キャプチャぶん並べた JSON の sha256 の先頭 16 桁。同じバイトが同じ順に動いた run は同じ名前になる。
置くときは `<case>__i2c-<kHz>k__pending` でもよい。validate が正しい名前を言う。

### provenance.yaml

| 項目 | 必須 | 内容 |
| --- | --- | --- |
| case | ○ | cases のキー |
| date | ○ | `YYYY-MM-DD` |
| targets | ○ | ケースと同じ形。空なら notes |
| bus | ○ | `{kind: i2c, hz, pins?, pullups?}` |
| dut | ○ | `{board, id?, core: {name, version}, controller}`。LibraryTest で probe 自身が DUT なら `id` は probe と同じ |
| library | library のケースで | `{key, version, commit?}`。key はケースのライブラリ |
| probe | ○ | `{id, firmware, version}` |
| wireskein | ○ | `{version}` |
| captures | ○ | `[{file, decode, timebase: {tick_hz, tick0, start_ns}, stored?, notes?}]` |
| notes | | |

- 版は分からなければ `unknown`。
- `timebase` は capture.json の `tick_hz` と `parts[0]` の `tick0`・`meta.start_ns` の写し。生のキャプチャを外しても
  転送を probe の時計に載せられるようにする。キャプチャがあるときは validate が突き合わせる。
- 生のキャプチャをリポジトリに置かないときは `stored: false` にし、notes に場所を書く。

OEP の run は `c0001.analysis.json`（wireskein decode の `wireskein-analysis/1`。版・指定・未解釈の区間・lost / gap を含む）も
置いている。`c0001.i2c.json` はその `transactions` を取り出したもの。EmbedBench の `skein2trace.py` が analysis.json を
直接読めるようになったら、`decode` をそちらに切り替える。

## 5. 振る舞い: knowledge/<chip>.yaml

| 項目 | 必須 | 内容 |
| --- | --- | --- |
| chip、bus | ○ | ファイル名は chip と同じ |
| addressing | | `pointer_bits`、`repeated_start_required`、`read_auto_increment`、`evidence`、`notes` |
| registers | ○ | `[{addr, width, access: r / w / rw, reset, name?, volatile?, per_specimen?, evidence?, notes?}]`。`addr` は `0x` ＋ 大文字の 16 進 2 桁、`reset` は width バイトの 16 進 |
| timing | | `[{key, value_us, windows?: [{setting, low_us, high_us, evidence}], evidence?, notes?}]`。`value_us` は模型が使う値（式でよい）、`windows` は記録が許す範囲 |
| behaviours | | `[{key, evidence, notes}]`。表で言えない振る舞い |

`evidence` は `runs/` の run の名前、`i2cdevicedb-pilot:<observation id>`、または `datasheet`。
validate は、chip がカタログにあることと、根拠がどれかに当たることを検査する。
`tools/knowledge_table.py <chip>` がレジスタの表を EmbedBench の `RegTableSpec` に書き出す。

## 6. ガイド: guides/chips/<chip>.md

`guides/README.ja.md` を見る。validate はリンク切れと、カタログに無いチップのガイドだけを検査する。
