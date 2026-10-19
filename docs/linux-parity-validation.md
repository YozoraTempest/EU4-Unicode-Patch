# Linux 功能对齐验证

本次改动增加 Linux 脚本旧转码、搜索资源、中文日期/标题、基于指令的程序校验，以及两平台 Ctrl+A。后续对齐也补齐了 Windows 无 BOM YAML 入口；两平台共用 `yaml_parse_buffer`，并以 `native_yaml` 验证 Windows 缓冲区和回调契约。Windows 搜索切换为同一 `search_resources` 加载器，保留已有 UTF-16LE INI。玩家行为的共同验收项见 [platforms.md](platforms.md#玩家功能对齐要求)。下列旧测试数量对应前一轮 Linux 改动，后续验证需单独记录。

## 本地自动测试

`build/launcher-integration` 完整 CTest 共 25 项，24 项通过。新增搜索配置、共享旧文本、Linux 原生脚本转换、程序映射检查和 Windows 原生选区布局夹具均通过。`paragraph_transport` 仍出现本主机既有的栅格容差差异：`map raster changed=32 difference=1684 coverage=240939`。本次未调整字体测试阈值。

Windows 选区实现已在本机用实际实现和原生布局夹具验证；这不等于 Windows 游戏画面的快捷键实测。跨平台 CI 结果另以关联 PR 记录为准。

## 实际 Linux ELF 接口

验证入口：`tools/linux-native-contract.cpp` 与 `tools/test-linux-native-contract.py`。运行真实游戏 ELF 和本轮 SO，由专用验证辅助库替换游戏 main，在游戏启动前运行原生接口，不进入战役，也不读取玩家 userdir。

- 真正的 YAML 解析、语言登记和查询：UTF-8/BOM、无 BOM、补充平面字符和旧 CP1252 包装转义均得到预期 UTF-8。四条注册成功、零拒绝；两份无 BOM 输入只在临时解析缓冲区补签名。
- 真正的 CTextLexer：UTF-8/BOM、无 BOM、引号内外的旧双字节转义均成功。四个旧转码词元、八个转义序列转换成功，零拒绝。
- 真正的年月、完整日期、战役和围城构造函数：采用受控本地化/月名/省名夹具，结果为 `1444年十一月`、`1444年十一月11日`、`北京之战`、`北京之围`。这验证原生接口与字符串所有权，不替代所有游戏日期调用方的 UI 回归。
- 真实预加载中的搜索资源发现：含空格的目录配合相对预加载路径，载入带 BOM 的自定义词典与配置，确认日志为 `custom-dictionary=1 typo-tolerance=0 fuzzy-pinyin=1`。
- 真正的 CTextBuffer 按键入口和 Select：空文档、中文/补充平面/组合文字和多行文档全选后，原生选区字节与完整文档一致，文档本身未改写。
- 原始 ELF 和末尾增加非代码数据的副本都加载成功并通过上述原生接口。关键词法入口改动一个字节的副本在安装挂钩前拒绝。原始游戏文件未改变。

原生游戏 SHA256：`af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3`。
本轮 SO SHA256：`e84d523a8d22de410bbd7fee07c0853ef9868f47df41e53233314fc3043d02de`。
本地原始证据：`build/parity-native/verified-contract-search/`，包含 JSON、标准输出、错误输出与补丁日志。早期失败夹具保留在同级其他测试目录，不归为通过结果。

可复现命令：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
python3 tools/test-linux-native-contract.py \
  --game "/path/to/Europa Universalis IV/eu4" \
  --build build --output build/native-contract-evidence
```

宿主机缺少游戏依赖时，可用 `--library-path` 指定已有运行库目录。工具自动加入原游戏目录，供游戏副本查找 `libsteam_api.so`。输出目录应为新的证据目录。

## 验证边界

本次原生接口验证没有操作实际 P 社启动器画面，没有完整启动所报告的 YTC 模组，也没有执行完整战役、保存/读档或 Windows GUI 全选。原生日志与固定产物只支持上述接口结论，不能据此认定用户此前的模组闪退已经修复。

## Windows 同步与共享规则验证（2026-10-20）

Windows 增加经过实际 PE 反汇编核对的 YAML 入口 `RVA 0x16fc3d0`。本轮检查了输入 vector 的 begin/end/capacity、引擎字符串和六参数调用；该编译版本将两个回调专门化为固定入口，原生验证工具也分别接管这两个入口。新增程序指令保护覆盖解析器序言及原生 BOM 检查。

- CI `39293130025`：Windows 31/31 CTest 通过，打包及加载保护通过；Arch 完整测试通过，Ubuntu 22.04 和 24.04 通过。Ubuntu 22.04 为 26 项通过、零失败、零跳过（仍按既有 CI 配置排除 `paragraph_transport`）。
- Windows 真实 PE 接口：使用 CI 玩家 DLL、链接 map 和只读映射的已安装游戏，8 组脚本文本和 6 组 YAML 均通过。YAML 使用生产适配器和生产旧转码导入函数，登记目标由受控捕获回调验证最终 UTF-8。覆盖无 BOM、带 BOM、旧转码、补充平面字符、颜色标记和正文 U+FEFF；只有 3 份缺少签名的输入补了临时签名，源缓冲区不变。
- Windows 未启动游戏入口、启动器或战役；这个结果不替代 Windows GUI/IME 实测，也不能据此宣布任意模组闪退已经修复。
- Ubuntu 22.04 CI SO 在本机 Arch 的真实 ELF 接口矩阵再次通过，包含 YAML 原生登记与查询、旧转码脚本、中文日期/标题、Ctrl+A、配置发现以及非代码改动接受/冲突指令拒绝。
- 本机完整 CTest 为 26/27 通过；唯一失败仍为原有 `paragraph_transport` 字体栅格容差，未修改测试阈值。

该轮产物源码提交为 `6d0fbb88ffddd534ca7c880adec1c9945aa7a300`，CI 合并提交为 `e6831adab004f8642b020a410a0ebf8442ed0924`，两者树均为 `e837677879d9c35539f4108e45ccf6354f0ece71`。后续清理只删除验证脚本中多余的测试替身，不改变产品源码。

Windows DLL SHA256：`c9b1c98e7fdeb4ce10697c75eab91a47c5286485c0652c9133e7e60efb6765e4`。
Ubuntu 22.04 SO SHA256：`c42131eb8403a4d4e0e6e5e4386c08d2434f37cefa59db71d818ce5987fed120`。
本地证据：`build/parity-native/windows/final-verification.json`、`build/parity-native/windows/native-import-report.json` 和 `build/parity-native/verified-ci-parity-6d0fbb8/`。

Windows 复现入口（64 位 Python，build 目录须含同一构建的玩家 DLL、map、build-info、executable_check 和 legacy_decode）：

```powershell
python tools/test-native-import.py --executable 'D:\SteamLibrary\steamapps\common\Europa Universalis IV\eu4.exe' --build-directory build --report build/native-import-report.json
```
