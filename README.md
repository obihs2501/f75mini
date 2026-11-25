# f75mini — AULA F75 极简配置工具

零依赖、单文件、Windows 原生 HID。配置**写入键盘板载内存**,改完即可关闭工具,
不常驻后台、不联网、不写注册表 —— 内存占用为 0,脚本本身 27 KB。

## 运行

需要 Python 3(任意 3.8+ 版本,无需 pip 安装任何包):

```powershell
python -X utf8 f75.py <命令>
```

(`-X utf8` 只是让 PowerShell 正确显示中文,可省略。)

## 命令

| 命令 | 作用 |
|---|---|
| `info` | 查看当前灯效、键位映射 |
| `effects` | 列出所有灯效 id、颜色编号、休眠取值 |
| `keylist` | 列出可修改的键名 |
| `light --effect N ...` | 改灯效 |
| `light --rgb FF0000` | 逐键自定义色(任意 RGB) |
| `key --list` | 查看当前键位映射 |
| `key --key 键名 --to 目标` | 改键位 |
| `dump` / `restore` | 备份 / 恢复全部配置 |

所有写命令都支持 `--dry-run`,先预览不写入。

## 常用示例

```powershell
# 改成静态红、亮度 7、最慢速度
python -X utf8 f75.py light --effect 1 --brightness 7 --speed 0 --colour 0

# 改成单个自定义色(逐键模式,任意 RGB)
python -X utf8 f75.py light --rgb FF0000 --brightness 9

# 呼吸灯、蓝色、休眠 5 分钟
python -X utf8 f75.py light --effect 2 --colour 2 --sleep 300

# 关灯
python -X utf8 f75.py light --effect 0

# 改键:把 LWin 改成禁用 / 改成 Esc / 改成 Ctrl+C
python -X utf8 f75.py key --key LWin --to disabled
python -X utf8 f75.py key --key LWin --to Esc
python -X utf8 f75.py key --key F12 --to LCtrl+C

# 改 Fn 层(--layer 1)
python -X utf8 f75.py key --layer 1 --key F1 --to LWin

# 备份 / 恢复
python -X utf8 f75.py dump --out f75-backup.bin
python -X utf8 f75.py restore f75-backup.bin
```

## 灯效 id

`0` 关闭、`1` 静态常亮、`2` 呼吸、`3` 彩虹、`4` 流光、`5` 雨滴、`6` 彩虹轮盘、
`7` 涟漪、`8` 星光、`9` 渐隐、`10` 贪吃蛇、`11` 霓虹、`12` 触发、
`13` 正弦波、`14` 扫描、`15` 风车、`16` 瀑布、`17` 花开、`18` 风暴、
`21` 自定义(逐键)。

颜色编号:`0` 红、`1` 绿、`2` 蓝、`3` 黄、`4` 品红、`5` 青、`6` 白、`7` 随机。

## Win 键说明

如果你的 Win 键被官方驱动改成了鼠标左键,先确认当前映射:

```powershell
python -X utf8 f75.py key --list
```

找到 `LWin` 那一行。若显示不是 `LWin`,改回:

```powershell
python -X utf8 f75.py key --key LWin --to LWin
```

或者长按 **Fn + Esc** 5 秒恢复出厂(会清掉所有自定义设置)。

注意:键盘有 Win 键锁定功能(**Fn + Win** 切换,锁定时 Win 键白灯常亮),
那是固件层的开关,不受键位映射影响;若映射正常但按了没反应,试试 Fn+Win 解锁。

## 技术说明

- 协议来自社区逆向:`trenbolone1122/aula-f75-linux`(Ghidra 逆向官方驱动)、
  `rosman21/aula75-remapper`、`vinicenter/aula-f75-webdriver`。
- 通路:USB-C 用 `258a:010c` 的 `MI_01/COL06` vendor collection,520 字节
  feature report `0x06`;2.4G 用 `3554:fa09` 的 usage page `0xFF02`,20 字节
  report `0x13`。自动优先有线。
- 安全:写前先读回校验;键位写回时强制带 `5A A5` 签名,否则键盘重启后会丢配置;
  写后再读回比对。灯块签名不匹配时直接拒绝写入。
- 若提示"无法打开设备(错误 5)",说明设备被其他程序(如官方驱动)独占,
  请先退出官方驱动;或以管理员身份运行。

## 来源与许可

社区逆向协议,本项目为独立实现,与 AULA 官方无关。
