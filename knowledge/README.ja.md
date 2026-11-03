# knowledge/

チップごとの振る舞いの表（`knowledge/<chip>.yaml`）。カタログ（`chips/`）の項目は繰り返さない。
形の正本は `docs/DATA_MODEL.ja.md` §5。QMP6988（レジスタ型）と SHT30（コマンド型）があり、どちらも EmbedBench の X68 で模型と照合済み。

- 値ごとに根拠（`evidence`）を書く: `runs/` の run の名前、`i2cdevicedb-pilot:<observation id>`、または `datasheet`。
- 時間は値と窓を分ける。`value_us` は模型が使う値（式でもよい）、`windows` は記録が許す範囲。
- `tools/knowledge_table.py <chip>` が、レジスタの表を EmbedBench の `RegTableSpec` に書き出す（模型の雛形）。
- `tools/knowledge_decl.py OUT` が、wireskein の `decl/devices`（部品の見分け）を書き出す。wireskein の I²C の定義は
  これの出力そのもの（wireskein edc2623 から）。knowledge の registers・commands・crc・identifies・アドレスを直したら、
  出力を wireskein に渡して取り込んでもらう。

I2CDeviceDB（commit 38f1d82）の `profiles/<chip>.yaml` は、ここへ移すまでの参照として残る。
