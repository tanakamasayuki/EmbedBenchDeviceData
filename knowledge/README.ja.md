# knowledge/

チップごとの振る舞いの表（`knowledge/<chip>.yaml`）。カタログ（`chips/`）の項目は繰り返さない。
形の正本は `docs/DATA_MODEL.ja.md` §5。レジスタ型（`registers`）とコマンド型（`commands`）がある。

- 値ごとに根拠（`evidence`）を書く: `runs/` の run の名前、`i2cdevicedb-pilot:<observation id>`、または `datasheet`。
- 時間は値と窓を分ける。`value_us` は模型が使う値（式でもよい）、`windows` は記録が許す範囲。
- 使う側の形への変換は使う側が持つ。どちらもこのリポジトリの clone を引数（`--devicedata DIR`）で受ける。
  - EmbedBench の `devices/tools/knowledge_table.py <chip>`: レジスタの表を `RegTableSpec` に書き出す（模型の雛形）。
  - wireskein の `tools/knowledge_decl.py`: `decl/devices`（部品の見分け）を書き出す。`--check` が、置いてある定義と
    今の knowledge から作ったものとのずれを報告する。registers・commands・crc・identifies・アドレスを直したら、
    wireskein の側で生成し直してもらう。

`knowledge/` の無いチップは、I2CDeviceDB（commit 38f1d82）の `profiles/<chip>.yaml` が参考になる（ガイドからリンクしてある）。
