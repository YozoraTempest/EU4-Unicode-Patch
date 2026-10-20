# 旧转码存档读取

## 原因与修复

EU4 1.37.5 的文本和二进制存档使用不同的词法解析器。此前 Windows 和 Linux 都只接管了 `CTextLexer::GetTok`，因此 `EU4txt` 的旧转码文字会转换为 UTF-8，而 `EU4bin` 中保存的同样文字会绕过转换。压缩是外层封装；不能仅凭 `.eu4` 扩展名或 ZIP 文件头判断内部使用哪条解析路径。

这会让本地化表提供的地名正常，而从旧存档恢复的军队、人物或自定义名称仍带有旧编码。截图中部分文字正常、部分混入异常符号与这个缺口相符，但截图本身不能确认玩家文件的内部格式。

本次同时接管两平台的 `CBinLexer::GetTok`：

| 平台 | 文本解析入口 | 二进制解析入口 |
| --- | --- | --- |
| Windows | RVA `0x170e700` | RVA `0x170ebf0` |
| Linux | VA `0x255e44e` | VA `0x255ecca` |

二进制解析器将带引号字符串分类为 `15`，未匹配词典的裸字符串分类为 `12`。数值也使用 `12`，因此只转换具有 `0x10`～`0x13` 旧协议标记的内容。转换发生在原生解析器完成字符串读取之后，沿用共享旧转码解码器及缓冲区边界检查，保留原生类型、输入位置、预读状态和 EOF 返回值。有效 UTF-8 原样保留；没有增加绘制时猜编码的路径。

新增入口的原生指令也加入现有程序兼容性检查。两平台分别统计二进制导入与拒绝数量。

## 验证（2026-10-20）

- Windows CTest：32/32 通过。
- Linux CTest：29/29 通过，包含 `paragraph_transport`。
- Windows 实际 PE 解析接口：18,244 组文本、36,482 组二进制、6 组 YAML 输入通过。样本包括既有旧 52 存档中提取的 18,236 条不同名称，以及 UTF-8、补充平面字符、旧重映射字符、混合西欧文字、预读重放、后续数值与 EOF。
- Linux 实际 ELF 接口：文本与二进制解析、UTF-8/BOM、预读重放、数值及 EOF 均通过；非代码改动仍被接受，冲突指令被拒绝。
- 完整存档原生解析：用既有旧 52 测试存档生成二进制夹具，逐个核对文本/二进制的 7,322,241 个原生 token 的类型和内容。不加载补丁时，两条原生路径的输出均有 30,309 个旧转码 token；加载修复后的补丁后两条路径的输出一致，均无旧协议标记。二进制路径转换 153,925 个旧转码单元，拒绝数为零。

Windows 实机另外启用了旧 52 主包 `2976470733` 和补充包 `1999055990`，在 1920×1080、GUI 1.3 下成功载入明国 `1445.1.1` 的普通旧档，确认战役画面与中文显示。这轮保存自动化没有产出可验证的另存文件，因此实机另存、重载未计为通过；也未测试玩家真实铁人档。

完整二进制夹具由游戏自己的文本解析器生成；结构符号使用二进制原生标记，其他 token 保留原字节和大小写，并通过两条原生解析路径逐 token 对照。它不是玩家铁人存档，不能替代铁人存档的实机读写、云存档或成就资格验证。

原始测试存档 SHA256：`79f22052ec815169cc8914f75590d3501d8a6b18c8dbeebda91e9b1abaa25d0f`。

验证 DLL SHA256：`44f32166bbefeaefc24eb9d249abb83ef9469849094839b0f8ec352115f48315`。

本地完整证据保存在 `build/legacy-save-investigation/`，原始存档保持不变。实际游戏载入结果及未完成的另存验证见该目录的运行报告；原生解析接口检查不冒充战役测试。

## 复现入口

Linux 在已构建插件和原生接口辅助库后运行：

```bash
python3 tools/test-linux-native-contract.py \
  --game '/path/to/Europa Universalis IV/eu4' \
  --build build \
  --output build/legacy-save-contract
```

Windows 使用同一次构建的 DLL、map、`build-info.json`、`executable_check.exe` 和 `legacy_decode.exe`：

```powershell
python tools/test-native-import.py `
  --executable 'D:\SteamLibrary\steamapps\common\Europa Universalis IV\eu4.exe' `
  --build-directory build `
  --report build/native-import-report.json
```

可选 `--script-values values.json` 同时扩展文本和二进制的名称样本。JSON 格式为含 `hex` 字段的对象数组；这里只读取样本，不改写源存档。
