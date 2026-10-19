# Linux 功能对齐验证

本次改动增加 Linux 脚本旧转码、搜索资源、中文日期/标题、基于指令的程序校验，以及两平台 Ctrl+A。Linux YAML 额外支持无 BOM 的 UTF-8。Windows YAML 入口本轮未修改；跨平台本地化素材仍建议保留 UTF-8 BOM，不能把脚本的 BOM 验证视为 Windows YAML 无 BOM 的验证。

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
