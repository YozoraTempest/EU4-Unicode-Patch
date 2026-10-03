# 字体与按需字形

开源字体模式使用进程私有 DirectWrite 集合：思源黑体 SC 优先，遍黑体 P1/P2 补充扩展汉字，其余字符沿用系统回退。字体不安装到 Windows，也不修改注册表。

| 字体 | 固定发行版 | cmap 非零映射数 | 用途 |
| --- | --- | --- | --- |
| Source Han Sans SC Regular | 2.005R | 44,853 | 中文界面和自身覆盖的汉字 |
| Plangothic P1 Regular | V2.9.5795 | 65,440 | 扩展汉字回退 |
| Plangothic P2 Regular | V2.9.5795 | 42,152 | 后续扩展区回退 |

来源：[Adobe 思源黑体](https://github.com/adobe-fonts/source-han-sans/releases/tag/2.005R)、[遍黑体项目](https://github.com/Fitzgerald-Porthmouth-Koenigsegg/Plangothic_Project)。三个原文件共 49,399,744 字节，约 47.1 MiB，均为 SIL OFL 1.1；保持原文件与名称，随包附带许可证。版本、提交、下载地址和 SHA-256 固定在 [字体清单](../fixtures/open-fonts.json)，授权见[第三方说明](../THIRD_PARTY_NOTICES.md)。

## 覆盖

fontTools 读取实际 cmap 并排除 glyph ID 0，再与官方 Unicode 17.0 已分配范围比较。统一汉字基本区、扩展 A–J、兼容汉字及补充区的 **102,998 个字符全部有非零映射**；三个字体映射并集为 135,063 个标量。输入字体和 Unicode 数据均校验固定哈希。

[覆盖报告](https://github.com/YozoraTempest/EU4-Unicode-Patch/blob/main/docs/evidence/open-font-coverage.json)证明字库映射，不能替代全部字符的游戏显示、IVS、复杂排版或图集容量验收。字体文件不需要一次性栅格化全部字符。

独立审计环境：

```powershell
.\tools\fetch-open-fonts.ps1
python -m venv private\font-analysis
.\private\font-analysis\Scripts\python.exe -m pip install -r tools\requirements-font-audit.txt
.\private\font-analysis\Scripts\python.exe tools\audit-open-fonts.py
```

审计脚本和依赖清单位于源码仓库；补丁运行不依赖 Python/fontTools。

## 运行时桥接

原夹具每字号只预生成 339 个字符，缺失汉字会取到省略号占位，源 UTF-8 不变。`prepare-test.ps1 -OpenFonts` 生成初始字符并预留固定 2048×4096 图集；缺失标量首次出现时，`scalar_glyph` 使用 DirectWrite / Direct2D / WIC 生成指标和 alpha，`native_font_atlas` 分配固定坐标并排队。

测宽线程只生成 CPU 数据。上传在游戏原生纹理查找路径中完成，沿用原纹理绑定和绘制；不扩大图集或重排旧坐标，因此已有顶点 UV 仍有效。SYSTEMMEM staging 从实际 DDS 初始化，再写入新字形。多个原生字体共享纹理时共用图集；稀疏记录保持稳定指针。

字体加载 `15953c0` 注册拥有者，纹理查找 `16c3f10` 刷新上传，字体表析构 `1594360` 释放绑定。DLL 不持有 default-pool 纹理引用；Reset 清除上传身份，重建纹理即使复用地址，也会从 CPU staging 恢复完整像素。最后一个拥有者释放 staging。

每页约 32 MiB GPU；首次动态上传另需 32 MiB CPU staging。当前五种字号各一页，约 160 MiB GPU，全部触发后约 160 MiB staging，另计缓存与字体资源。单页填满或实际缺字时仍走占位；多页、淘汰和全局预算尚未完成。普通系统/工坊小图集保留预生成覆盖。

## 排版与验证

当前桥接按单标量生成位图。独立 `unicode_layout` 已保存实际字体、视觉基线、方向、advance/offset 和 UTF-8 cluster，并可直接栅格化完整 shaped run；字体对象随 run 持有。run 栅格上限为单边 16,384 像素、64 MiB alpha。这套整段布局尚未连接游戏 GPU、测宽和选区，逐标量图集不能替代阿拉伯文等复杂文字排版。

真实 EU4 的 16px 外交搜索框已通过三个完整提交、14 个实际 GPU 字形区域逐像素核对，其中 11 个是动态新增。GPU 探针确认字体管理器资源就是设备实际绑定的纹理，本次绘制返回位置为 `159b151`；旧只读缓存分支调查的 `159b3b0` 属于另一个调用位置。另一个真实 D3D9 设备测试通过上传、旧区域保留、Reset、纹理地址复用及释放。证据与范围统一列在[测试记录](validation.md)。

更多字号、地图新增字形、复杂文字、游戏设备恢复与长期增长仍需逐项验证。构建检查见[构建说明](build.md)，实际探针重建见[开发说明](development.md)。
