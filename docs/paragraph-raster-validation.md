# 段落栅格失败的诊断与修复

2026-10-20 更正：此前将本机 `paragraph_transport` 失败仅归为字体环境或抗锯齿容差，判断不完整。它同时暴露了 Linux 栅格器的真实裁切问题，以及测试比较重叠字形时的合成假设问题。

## 实际裁切

Linux 的 `rasterize_glyph_run` 用 Cairo recording surface 的 ink extents 分配紧贴字形的 A8 图像。小数基线下，Cairo 的相位化字形位图可能比该度量边界多覆盖一像素。紧贴边界的目标图像会裁掉边缘，而整段渲染因其他字形扩大了目标范围，保留了同一行像素。因此只比较整段时不一定暴露问题，拆成地图字簇后会出现缺墨。

采用同一份 DejaVu Sans 字体可稳定复现：24pt 的希伯来字母 `ר`、`ב`，以及 16pt 的 `a` 加上下组合标记。直接让 Pango 在宽裕的 A8 画布上绘制原文，可得到独立参考；该参考不经过本项目的 glyph 提取、recording extents 或裁剪代码。

修复在四周增加一像素的临时绘制余量，然后扫描实际非零 alpha 并裁回紧凑位图，同时修正 `left/top`。保留原有字体、字形、字形位置及小数相位，透明余量不会占用最终字库空间。

## 测试假设

原测试用 `max` 合成独立字簇，再以整段图像的总覆盖率计算百分比容差。Cairo 整段绘制时，重叠字形掩码可按饱和加法累计；该结果与 `max` 不同。全局百分比也不能区分正常的重叠合成与真实的少量缺墨。

固定输入 `A سلام हिन्दी ạ́ עברית` 的本机差异可拆分为：

| 成分 | 差异像素 | alpha 差值之和 |
| --- | ---: | ---: |
| 原始 max 比较的全部差异 | 32 | 1684 |
| 重叠掩码采用不同合成方式 | 17 | 788 |
| 被实际裁掉的边缘 | 15 | 896 |
| 修复后按 Cairo 饱和加法重建 | 0 | 0 |

共享测试改为逐像素检查覆盖上下界：下界为各字簇 alpha 的最大值，上界为它们的饱和加法。重叠处允许不同栅格后端的合成规则；无重叠处上下界相同，要求逐像素完全一致，不使用百分比容差。测试还故意移除一个覆盖像素，确认它必定失败。

## 独立回归与 CI

`linux_glyph_raster` 使用固定的 DejaVu Sans 家族，对 6 组文字、7 个字号和 4 种水平相位执行 168 次直接 Pango/A8 与生产栅格器的逐像素比较，覆盖组合文字、RTL、升降部以及四种子像素相位。参考和产品都使用 A8；不能拿 ARGB32 的不同 Cairo 合成路径作为逐像素 alpha 字库基准。

Ubuntu 22.04 的 Cairo 1.16 将 glyph 位置取整。裁掉透明边框后，四分之一像素移动可能既不改变实际覆盖，也不改变 alpha；原 `unicode_layout` 测试强制要求 alpha 改变，属于错误假设。该断言改为遍历相位并同时检查覆盖与位置；168 组独立绘制对比另外逐像素验证实际 Cairo 取整结果。没有按版本跳过测试。

修复前后的本地验证：

- 将旧栅格器换回、新测试保持不变：`paragraph_transport` 和 `linux_glyph_raster` 均失败，0/2 通过。地图首次越界像素为 `(172,19)`：参考 alpha 为 52，实际最大值和累计值均为 0。
- 恢复修复后的实现：完整 CTest 28/28 通过，零跳过；168 次直接 Pango 对比均完全一致。
- 未增加原有百分比阈值；共享比较已用逐像素覆盖约束取代阈值。
- Ubuntu CI 移除 `paragraph_transport` 排除项，Arch 和 Ubuntu 都安装 DejaVu 测试字体并运行完整测试。游戏运行时仍使用原有模组字体及字体回退策略，DejaVu 是测试依赖。

复现：

```bash
cmake --build build/launcher-integration
ctest --test-dir build/launcher-integration --output-on-failure -R 'paragraph_transport|linux_glyph_raster'
ctest --test-dir build/launcher-integration --output-on-failure
```

本地原始证据位于 `build/paragraph-raster-investigation/`：`report.json`、`baseline-strict-tests.log`、`final-ctest.log`、前后 PGM 掩码与 `raster-comparison.png`。测试字体 SHA256 为 `6038a160b491e121c1f12c7bccb4a9c8730296e3adc1086a059404ed84b7451c`；本机 Cairo 1.18.6、Pango 1.58.2。

底层 API 依据：Cairo 的 recording surface ink extents 用于估计重放目标范围；实际图像后端的子像素 glyph cache 与掩码累计路径需另行验证。可查 Cairo 官方的 Recording Surfaces、scaled-font 文档，以及 1.18.6 的 `cairo-image-compositor.c` 和 `cairo-scaled-font.c`。
