# F75 极简驱动

AULA F75 键盘配置工具。协议来自社区逆向,不依赖官方驱动。

## 三个版本

| 版本 | 文件 | 体积 | 运行依赖 |
|---|---|---|---|
| **原生 C(主推)** | `f75.exe` / `f75.c` | **201 KB** | 无 |
| Web 可视化 | `f75-studio.html` | 40 KB | Chrome/Edge(WebHID) |
| Python 脚本 | `f75.py` | 27 KB | Python 3 |

配置写入键盘板载内存,改完不需要任何后台进程。

## 原生 C 版用法

```powershell
f75.exe info                                    # 查看状态
f75.exe light --effect 1 --brightness 7 --colour 0   # 静态红
f75.exe light --rgb FF0000                      # 逐键自定义色
f75.exe key --list                              # 查看键位
f75.exe key --key LWin --to LWin                # 改键
f75.exe dump --out backup.bin                   # 备份
f75.exe restore backup.bin                      # 恢复
```

灯效 ID:`0`关闭 `1`静态 `2`呼吸 `3`彩虹 `4`流光 `5`雨滴 `6`彩虹轮盘
`7`涟漪 `8`星光 `9`渐隐 `10`贪吃蛇 `11`霓虹 `12`触发 `13`正弦波
`14`扫描 `15`风车 `16`瀑布 `17`花开 `18`风暴 `21`逐键自定义

颜色:`0`红 `1`绿 `2`蓝 `3`黄 `4`品红 `5`青 `6`白 `7`随机

`--to` 写法:`LWin` / `disabled` / `LCtrl+C` / `raw:00:08:00:00`

## Web 可视化版

```powershell
.\启动配置器.bat
# 浏览器打开 http://localhost:8765/f75-studio.html
```

Chrome/Edge 直接通过 WebHID 通信,三套主题(精密仪器/工作台/暗夜),
无需后端。点"演示模式"可先看界面。

## GitHub 编译

仓库:https://github.com/obihs2501/f75mini

推送到 main 会自动用 Windows-latest + MSVC 编译并上传 artifact:

```powershell
git add . && git commit -m "update" && git push
```

手动编译(本机需 MSVC):

```powershell
cl /nologo /O2 /utf-8 /Fe:f75.exe f75.c
```

## Win 键说明

F75 的 Win 键锁定由 **Fn+Win** 控制(锁定时该键白灯常亮),
这是固件层开关,不受键位映射影响。若映射正常但按了没反应,按 Fn+Win 解锁。
键位映射本身可用 `key --key LWin --to ...` 修改;长按 Fn+Esc 5 秒可整板恢复出厂。

## 技术

- 协议逆向来源:`trenbolone1122/aula-f75-linux`、`rosman21/aula75-remapper`、
  `vinicenter/aula-f75-webdriver`
- USB-C:`258a:010c` 的 `MI_01/COL06`,520 字节 feature report `0x06`
- 2.4G:`3554:fa09` 的 usage page `0xFF02`,20 字节 report `0x13`
- 写前读回校验;键位写回强制带 `5A A5` 签名,否则重启后丢失

本项目为独立实现,与 AULA 官方无关。MIT。
