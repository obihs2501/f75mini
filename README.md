# F75 配置工具

面向 AULA F75 三模键盘的 Windows 原生中文配置工具。**当前是只读预览版，不是官方驱动的完整替代品。** 真实设备写入未通过本项目的真机验收，程序不提供灯光、键位、宏、配置恢复或恢复出厂的硬件写入口。

## 下载与运行

在 [GitHub Actions](https://github.com/obihs2501/f75mini/actions/workflows/build.yml) 打开最近一次成功的构建，从 **Artifacts** 下载 `f75-windows-x64`。GitHub 可能要求登录后下载。解压后双击 `F75Config.exe`；命令行工具为同目录下的 `f75-cli.exe`。

目标平台为 **Windows 10 22H2 / Windows 11，x64**。CI 使用 Windows Server 2022 和 MSVC 编译；桌面系统版本、高 DPI 和实际键盘仍需人工复核。运行不需要安装、管理员权限、网络、官方驱动、后台服务或浏览器运行时。暂不支持通过蓝牙配置。

## 可以做什么

- 连接 USB-C 有线或 2.4G 接收器，扫描、选择候选接口并手动读取。接收器被发现不等于键盘在线；无响应可先唤醒键盘、检查供电和距离，再重试。
- 显示有实际响应依据的读数；因通路、固件或报文校验无法读取的项不会填入示例值。基础键层的协议矩阵不是已确认的逐键映射，不能据此断言 Win 键被重映射。
- 查看 Win 键排查步骤：先检查连接与窗口输入，再确认 Windows 模式，之后才按对应手册检查 **Fn+Win**。固件锁定无法由本工具读取或控制。恢复映射也不等于解锁。
- 进入隔离的 ANSI 演示，在键盘图上编辑、撤销、应用本地灯光和键位，或保存与导入 `.f75demo`。演示从不向硬件写入。
- 导出真实的可读取配置为版本化 UTF-8 JSON，**不是完整备份，也不能恢复到键盘**。JSON 明确标记不能恢复、布局未核验；未读到的字段为 `null`，不导出本机 HID 路径。

## 命令行

```powershell
.\f75-cli.exe --help
.\f75-cli.exe list
.\f75-cli.exe info --device 1 --json
.\f75-cli.exe export --device 1 --out .\f75-snapshot.json
.\f75-cli.exe demo create .\example.f75demo
.\f75-cli.exe demo validate .\example.f75demo
```

设备序号从 1 开始，拔插后请先重新列出候选设备。导出和演示创建不会覆盖已有文件。JSON 输出为 UTF-8。退出码：`0` 成功；`2` 参数错误；`3` 设备选择失败；`4` 未确认响应；`5` 文件错误；`6` 内部错误。

## 开发与验收

本仓库在 GitHub Actions 云端构建 x64 Release；通过测试后上传便携包。设计依据见 [初步修订版 PRD](PRD-初步修订版.md)，实际验证边界和放行条件见 [验收说明](docs/validation.md)。本项目与 AULA / EPOMAKER 无隶属关系。
