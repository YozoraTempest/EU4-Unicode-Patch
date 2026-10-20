# 独立诊断工具

诊断工具作为同一 Release 的额外附件发布，不包含在 Windows 主包、可选字体包或 Linux 通用补丁包中。可以解压到任意可写目录，不需要放进游戏目录，也不要求管理员权限。

| 平台 | 独立产物 | 运行需求 |
| --- | --- | --- |
| Windows x64 | `EU4UnicodePatch-diagnostics-windows-x86_64-<版本>.zip` | Windows 自带的 64 位 PowerShell 5.1，Windows 10 1903 或更新版本 |
| Linux x86_64 | `EU4UnicodePatch-diagnostics-linux-x86_64-<版本>.tar.gz` | Python 3；探针使用与通用补丁相同的系统文字运行库 |

Windows 可双击 `diagnose-windows.cmd` 自动寻找 Steam 安装，也可在终端明确指定游戏路径：

```bat
diagnose-windows.cmd -GameDirectory "D:\SteamLibrary\steamapps\common\Europa Universalis IV" -OutputDirectory "D:\Reports"
```

Linux：

```sh
bash diagnose-linux.sh "/path/to/Europa Universalis IV" --output-directory "/path/to/reports"
```

Linux 可额外使用 `--patch-directory "/path/to/downloaded/patch"` 检查尚未安装的补丁。存在多个游戏副本时应明确指定路径；工具不会猜选某一个副本。未安装 Python 3 时，Linux 外层脚本会先给出提示。

每次运行生成带时间标记的 `.json` 和 `.txt` 两份报告。工具只读游戏、补丁、启动器设置和已有补丁日志，不启动游戏、不修复安装、不下载依赖，不扫描模组内容，不上传报告。报告含本地安装路径和选定环境信息，反馈时可自行决定分享范围。

## 检查范围

- 工具自身的版本、来源提交及对应补丁哈希；安装版本不同时报告差异，不将哈希差异直接等同为不兼容。
- Windows 的游戏 PE 架构、关键指令和映像布局、加载器与补丁文件、原启动器入口。原生检查映射程序，不运行入口。
- Linux 的游戏 ELF 架构与可执行权限、补丁依赖、启动器包装入口与原始备份，以及 ICU、SDL2 ABI 和文字运行库。ELF 架构检查不替代实际补丁安装挂钩前的关键指令检查。
- 两平台系统字体对中文、补充平面字、阿拉伯文、希伯来文、天城文和泰文样例的实际字形覆盖。模组字体与可选私有字库会改变实际游戏覆盖，本报告不据此断言所有模组文本都会显示。
- 若同一个游戏正在运行且可读取，另行记录它加载的补丁、ICU、SDL 和相关文字模块路径。没有游戏进程时明确记为未验证。
- 既有补丁日志中的关键记录与修改时间，不把旧日志当作本轮启动结果。

诊断进程初始化成功只能说明当前诊断运行环境可用。Steam Flatpak、Steam Linux Runtime、桌面输入法会话和游戏 GPU 状态应在各自环境验证。缺少运行库导致探针无法启动时，外层工具仍生成报告并给出对应发行版的依赖安装建议。

退出码 `0` 表示没有失败项，可能存在警告；`1` 表示发现问题且已生成报告；`2` 表示参数或工具启动错误。
