# Unicode 存档与原生路径验收

目标版本：Steam EU4 1.37.5 Windows x64。所有文件位于项目的独立测试用户目录；原游戏与已有用户存档未参与写入。

## 已验证的数据路径

原生压缩存档 `持久化𠀀1444_11_11.eu4` 包含以下 UTF-8 数据：

| 原生记录 | 名称 |
| --- | --- |
| 法国首都省份 -183 的 name | 巴黎𠀀 |
| 该省份的 capital | 中文😀 |
| 法国 army 的 name | 中文𠀀测试军 |
| 法国 navy 的 name | 中文😀测试舰队 |

保存后移除测试事件和法国单位名称模板，重启读取原文件，再通过游戏原生保存窗口生成非压缩 `UTF8𠀀1444_11_11.eu4`。四类名称及初始化标志均在新文件的对应原生记录中完整保留。随后重启读取该非压缩文件，军队面板仍显示持久化名称。

对应证据：[压缩文件检查](evidence/persistence-compressed.json)、[非压缩文件检查](evidence/persistence-plaintext.json)、[转换与文件打开记录](evidence/persistence-roundtrip.json)、[重新读取后的军队](evidence/unicode-persistence-reloaded.jpg)、[非压缩文件重启读取](evidence/unicode-persistence-plaintext-reloaded.jpg)、[无代理原生 Load Game](evidence/unicode-persistence-load-game.jpg)。数据由夹具事件及原生名称模板生成；这项验收不证明输入法或编辑器已经通过。

## 修正的两处读取问题

原生 UTF-16 → UTF-8 路径转换函数 `0x19fc030` 验证代理对后遗漏 `+0x10000`。例如 U+20000 被转换成 U+10000，导致文件枚举后的读取路径指向不存在的文件。补丁只修正该有效代理对分支，继续使用原生编码器的容量控制。

Load Game 的名称选择入口还会调用 CP1252 转写函数。它与保存名称构造、保存按钮的调用一并绕过，保留标准 UTF-8；文件系统路径检查仍由游戏执行。不能仅凭 Continue Game 进入战局判断读取成功：旧版本打开失败后可能初始化默认世界。

**验收更正：** `fd08cff` 的非 BMP 重启读取记录是误判。移除名称模板后，旧 DLL 显示默认法国军队和巴黎名称；CreateFileW 跟踪确认 U+20000 路径被误写成 U+10000，返回无效句柄。该版本已证明原生写入，尚未证明正确读取。当前记录将该旧读取结论撤回，新验收要求原生文件打开成功并核对持久化数据。

原生转换函数实测覆盖 ASCII、基本汉字、U+10000、U+20000、U+1F600、U+10FFFF；UTF-16 / UTF-8 双向转换均一致。5 字节输出缓冲区保存一个四字节字符及终止符，追加字符被完整截断。路径跟踪只公开测试文件名、调用地址和句柄成功状态，不公开绝对用户路径或完整存档。

## 非 BMP 路径比较

原生 UTF-16 不区分大小写比较函数 `19fbb90` 的左右代理对分支也遗漏 `+0x10000`。补齐 `19fbc23` 与 `19fbca2` 两处解码后，仍使用原生大小写展开、非法代理项处理及比较返回契约。

实际调用验证 9 组正反比较共 18 项：ASCII、ß → SS、基本汉字、U+10000 与空串、U+10000 后续字符、U+10041 与 ASCII A、不同非 BMP 平面、Deseret 大小写、emoji 与 U+10FFFF。新增比较及原有 6 组转换和容量边界均通过，见 [运行记录](evidence/native-path-comparison.json)。验证使用新分配的缓冲区，未改写游戏对象或存档。DLL SHA-256 为 `dc09995c74975c005949e73d3c1d7ea1e6d289effe525bb02bca7bf0cee34276`。

## 本地月度自动保存

移除名称生成事件和单位模板后，从已验证的非压缩文件推进时间。1444.12.1 与 1445.1.1 的原生月度自动保存均保留上述四类名称及初始化标志；第二次保存将十二月文件轮换到 `old_autosave.eu4`，SHA-256 与轮换前完全一致。

退出测试实例，将私有设置切换为压缩自动保存，再通过 Continue 读取一月文件。1445.2.1 生成的压缩 `autosave.eu4` 通过 ZIP 和原生字段检查。随后恢复原私有设置、重启读取该压缩自动存档，通过原生保存窗口生成非压缩 `法兰西1445_02_01.eu4`；日期仍为 1445.2.1，四类名称与标志再次完整保留。该流程没有附加代理，也没有重新生成名称。

见 [自动保存往返及五份文件指纹](evidence/autosave-roundtrip.json)、[非压缩自动保存](evidence/autosave-plaintext.json)、[压缩自动保存](evidence/autosave-compressed.json)、[重启后的非压缩文件](evidence/autosave-reloaded-plaintext.json)及[重启日期画面](evidence/autosave-reloaded.jpg)。这项验收覆盖本地月度保存、文件轮换、两种格式读取和名称持久化；其他周期、云存档与铁人仍需独立验证。

## 重建夹具

退出测试游戏后执行：

```powershell
.\tools\prepare-test.ps1 -SystemFonts -PersistenceProbe
.\tools\start-test.ps1
```

选择法国开始新战局，确认持久化测试事件，使用原生保存窗口生成压缩存档。初始化事件设置标志，单位模板仅影响新生成的单位；验证读取时必须移除二者：

```powershell
# 先退出测试实例；此命令重建私有夹具，移除持久化事件和单位模板。
.\tools\prepare-test.ps1 -SystemFonts
.\tools\start-test.ps1
```

读取刚保存的文件，核对军队、省份名称。取消 Compress，在另一个文件名下保存非压缩文件；避免覆盖原来的测试样本。只读校验脚本同时接受这两种格式：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-persistence-save.py 'private/test-userdir/save games/持久化𠀀1444_11_11.eu4'
..\EU4MenuPatch\.venv\Scripts\python.exe tools\verify-persistence-save.py 'private/test-userdir/save games/法兰西1444_11_11.eu4'
```

第二个文件名由重建后的默认法国本地化生成，实际验收使用临时私有 `UTF8𠀀` 本地化以区分两份文件。脚本对 -183 的所有权、对应原生字段、初始化标志和 ZIP 完整性进行检查；只有字符串出现在其他地方不会通过。

运行 `start-test.ps1` 后，可附加独立路径跟踪：

```powershell
..\EU4MenuPatch\.venv\Scripts\python.exe tools\trace-paths.py --duration 300
```

跟踪要求精确的隔离 EXE、已启用的研究 DLL及单一测试实例。转换测试只使用新分配的缓冲区；文件跟踪观察游戏的原生操作。跟踪时间结束时关闭该专用实例后再移除代理。

## 仍需验证

其他自动保存周期、云存档、输入法产生的文件名、存档名编辑、铁人及联机文本仍需独立验收。省份与单位的数据往返不能替代这些入口的实际测试。
