#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""f75gui.py — AULA F75 图形配置界面( tkinter,零第三方依赖 )

复用 f75.py 的协议核心。运行:  python -X utf8 f75gui.py
"""

import sys
import time
import tkinter as tk
from tkinter import ttk, messagebox, colorchooser

import f75 as core


EFFECT_NAMES = ["%d - %s" % (i, n) for i, n in core.EFFECTS]


class App:
    def __init__(self, root):
        self.root = root
        root.title("AULA F75 配置器")
        root.geometry("720x560")
        root.minsize(680, 520)

        self.dev = None
        self.path = self.kind = self.cmds = None
        self.km = None
        self.led = None
        self.selected_key = tk.StringVar(value="LWin")

        self._build()
        self.refresh()

    # ---------------------------------------------------------- UI
    def _build(self):
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill="x")
        ttk.Label(top, text="AULA F75 配置器", font=("Microsoft YaHei UI", 14, "bold")).pack(side="left")
        ttk.Button(top, text="刷新状态", command=self.refresh).pack(side="right")
        ttk.Label(top, text="连接: 检测中…", foreground="#888").pack(side="right", padx=8)
        self.lbl_conn = top.winfo_children()[-1]

        nb = ttk.Notebook(self.root)
        nb.pack(fill="both", expand=True, padx=8, pady=(0, 8))
        self.tab_light = ttk.Frame(nb, padding=12)
        self.tab_key = ttk.Frame(nb, padding=12)
        self.tab_backup = ttk.Frame(nb, padding=12)
        nb.add(self.tab_light, text="灯光")
        nb.add(self.tab_key, text="键位")
        nb.add(self.tab_backup, text="备份 / 恢复")

        self._build_light(self.tab_light)
        self._build_key(self.tab_key)
        self._build_backup(self.tab_backup)

        bot = ttk.Frame(self.root, padding=8)
        bot.pack(fill="x")
        self.dry = tk.BooleanVar(value=False)
        ttk.Checkbutton(bot, text="dry-run(只预览不写入)", variable=self.dry).pack(side="left")
        ttk.Label(bot, text="配置写入键盘板载内存,关掉本窗口后依然生效",
                  foreground="#888").pack(side="right")

    def _build_light(self, p):
        f = ttk.LabelFrame(p, text="灯效", padding=12)
        f.pack(fill="x")
        self.effect = tk.StringVar()
        cb = ttk.Combobox(f, textvariable=self.effect, values=EFFECT_NAMES, state="readonly", width=28)
        cb.pack(side="left", padx=(0, 12))

        self.colour = tk.IntVar(value=0)
        ttk.Label(f, text="颜色:").pack(side="left")
        ccb = ttk.Combobox(f, textvariable=self.colour,
                           values=list(range(len(core.COLORS))), state="readonly", width=6)
        ccb.pack(side="left", padx=(0, 8))
        self.colour_name = tk.StringVar(value=core.COLORS[0])
        ttk.Label(f, textvariable=self.colour_name).pack(side="left")

        self.preview = tk.Canvas(f, width=28, height=28, highlightthickness=1,
                                 highlightbackground="#999")
        self.preview.pack(side="left", padx=12)

        self.bright = tk.IntVar(value=7)
        self.speed = tk.IntVar(value=0)
        row = ttk.Frame(p, padding=(0, 10))
        row.pack(fill="x")
        ttk.Label(row, text="亮度:").pack(side="left")
        tk.Scale(row, from_=0, to=9, orient="horizontal", variable=self.bright,
                 length=180, showvalue=True).pack(side="left", padx=8)
        ttk.Label(row, text="速度:").pack(side="left", padx=(16, 0))
        tk.Scale(row, from_=0, to=4, orient="horizontal", variable=self.speed,
                 length=140, showvalue=True).pack(side="left", padx=8)

        row2 = ttk.Frame(p)
        row2.pack(fill="x", pady=8)
        ttk.Label(row2, text="自定义 RGB(逐键):").pack(side="left")
        self.rgb = tk.StringVar(value="FF0000")
        ttk.Entry(row2, textvariable=self.rgb, width=10).pack(side="left", padx=6)
        ttk.Button(row2, text="选色…", command=self.pick_rgb).pack(side="left", padx=4)
        ttk.Button(row2, text="应用灯效", command=self.apply_light).pack(side="left", padx=12)

        row3 = ttk.Frame(p)
        row3.pack(fill="x", pady=4)
        ttk.Label(row3, text="休眠:").pack(side="left")
        self.sleep = tk.IntVar(value=300)
        ttk.Combobox(row3, textvariable=self.sleep,
                     values=[s for s, _ in core.SLEEP_VALUES], state="readonly", width=8).pack(side="left", padx=6)
        ttk.Button(row3, text="关闭屏幕灯效(关灯)", command=self.turn_off).pack(side="left", padx=12)

        self.lbl_light = ttk.Label(p, text="当前: 未读取", foreground="#555")
        self.lbl_light.pack(anchor="w", pady=8)

        # trace 更新预览
        self.colour.trace_add("write", lambda *_: self._update_preview())
        self.rgb.trace_add("write", lambda *_: self._update_preview())

    def _build_key(self, p):
        f = ttk.Frame(p)
        f.pack(fill="both", expand=True)
        left = ttk.Frame(f)
        left.pack(side="left", fill="y")
        ttk.Label(left, text="键位(点击选择):").pack(anchor="w")
        self.key_list = tk.Listbox(left, width=14, height=20, exportselection=False)
        self.key_list.pack(side="left", fill="y")
        self.key_list.bind("<<ListboxSelect>>", self.on_key_select)
        for name in sorted(core.KEY_INDEX):
            self.key_list.insert(tk.END, name)

        right = ttk.LabelFrame(f, text="映射", padding=12)
        right.pack(side="left", fill="both", expand=True, padx=(12, 0))
        ttk.Label(right, text="选中键:").pack(anchor="w")
        self.lbl_sel = ttk.Label(right, text="", font=("Consolas", 11, "bold"))
        self.lbl_sel.pack(anchor="w", pady=2)
        ttk.Label(right, text="当前映射:").pack(anchor="w", pady=(12, 0))
        self.lbl_cur = ttk.Label(right, text="", font=("Consolas", 11), foreground="#0a6")
        self.lbl_cur.pack(anchor="w")
        ttk.Label(right, text="新映射(如 LWin / Esc / LCtrl+C / disabled / raw:00:08:00:00):").pack(anchor="w", pady=(16, 0))
        self.to = tk.StringVar(value="LWin")
        ttk.Entry(right, textvariable=self.to, width=32).pack(anchor="w", pady=4)
        ttk.Button(right, text="应用键位", command=self.apply_key).pack(anchor="w", pady=8)
        ttk.Button(right, text="恢复该键出厂映射", command=self.reset_key).pack(anchor="w", pady=2)
        self.lbl_key = ttk.Label(right, text="", foreground="#555")
        self.lbl_key.pack(anchor="w", pady=8)

    def _build_backup(self, p):
        ttk.Label(p, text="把当前键盘的全部键位与灯光配置保存到文件:").pack(anchor="w")
        row = ttk.Frame(p)
        row.pack(fill="x", pady=6)
        self.backup_path = tk.StringVar(value="f75-backup.bin")
        ttk.Entry(row, textvariable=self.backup_path, width=40).pack(side="left", padx=(0, 8))
        ttk.Button(row, text="浏览…", command=self.pick_backup).pack(side="left", padx=4)
        ttk.Button(row, text="备份", command=self.do_backup).pack(side="left", padx=4)
        ttk.Label(p, text="从备份恢复(会覆盖键盘当前配置):").pack(anchor="w", pady=(20, 0))
        row2 = ttk.Frame(p)
        row2.pack(fill="x", pady=6)
        self.restore_path = tk.StringVar(value="f75-backup.bin")
        ttk.Entry(row2, textvariable=self.restore_path, width=40).pack(side="left", padx=(0, 8))
        ttk.Button(row2, text="恢复", command=self.do_restore).pack(side="left", padx=4)
        self.lbl_backup = ttk.Label(p, text="", foreground="#555")
        self.lbl_backup.pack(anchor="w", pady=8)

    # ---------------------------------------------------------- helpers
    PALETTE_HEX = ["#ff0000", "#00ff00", "#0000ff", "#ffff00",
                   "#ff00ff", "#00ffff", "#ffffff", "#888888"]

    def _update_preview(self):
        idx = self.colour.get()
        if 0 <= idx < len(core.COLORS):
            self.colour_name.set(core.COLORS[idx])
        try:
            s = self.rgb.get().strip().lstrip("#")
            if len(s) == 6:
                r, g, b = (int(s[i:i + 2], 16) for i in (0, 2, 4))
                hexs = "#%02x%02x%02x" % (r, g, b)
            else:
                hexs = self.PALETTE_HEX[idx] if 0 <= idx < 8 else "#888888"
        except Exception:
            hexs = "#888888"
        self.preview.delete("all")
        self.preview.create_oval(3, 3, 25, 25, fill=hexs, outline="#333")

    def open_dev(self):
        if self.dev:
            return True
        self.path, self.kind, self.cmds = core.find_device(None)
        if not self.path:
            messagebox.showerror("未找到键盘", "请插上 USB-C 线或 2.4G 接收器。")
            return False
        try:
            self.dev = core.Device(self.path, self.kind, self.cmds)
        except Exception as e:
            messagebox.showerror("打开失败", str(e))
            return False
        return True

    def refresh(self):
        if not self.open_dev():
            self.lbl_conn.config(text="连接: 未找到键盘", foreground="#c00")
            return
        kind = "USB-C 有线" if self.kind == "wired" else "2.4G 接收器"
        self.lbl_conn.config(text="连接: " + kind, foreground="#0a0")
        try:
            self.led = core.read_led_block(self.dev)
            name, bright, speed, colour = core.led_info(self.led)
            self.lbl_light.config(text="当前: %s  亮度 %s  速度 %s  颜色 %s" %
                                  (name, bright, speed, colour))
            names = [e[1] for e in core.EFFECTS]
            self.effect.set(EFFECT_NAMES[names.index(name)] if name in names else EFFECT_NAMES[0])
            if colour is not None:
                self.colour.set(core.COLORS.index(colour))
            if bright is not None:
                self.bright.set(bright)
            if speed is not None:
                self.speed.set(speed)
        except Exception as e:
            self.lbl_light.config(text="灯效读取失败: %s" % e, foreground="#c00")
        try:
            self.km = core.read_keymap(self.dev, 0)
            self.on_key_select(None)
        except Exception as e:
            self.lbl_key.config(text="键位读取失败: %s" % e, foreground="#c00")

    def on_key_select(self, ev):
        sel = self.key_list.curselection()
        if not sel:
            return
        name = self.key_list.get(sel[0])
        self.selected_key.set(name)
        self.lbl_sel.config(text="%s (矩阵索引 %d)" % (name, core.KEY_INDEX[name]))
        if self.km is not None:
            idx = core.KEY_INDEX[name]
            code = bytes(self.km[idx * 4:idx * 4 + 4])
            self.lbl_cur.config(text=core.code_text(code))
            self.to.set(core.code_text(code))

    # ---------------------------------------------------------- actions
    def apply_light(self):
        if not self.open_dev():
            return
        try:
            sel = self.effect.get()
            mode = int(sel.split(" - ")[0])
            rgb = self.rgb.get().strip().lstrip("#")
            use_rgb = len(rgb) == 6
            b = core.read_led_block(self.dev)
            slot = next((i for i, e in enumerate(core.EFFECTS) if e[0] == mode), None)
            if slot is None:
                messagebox.showerror("错误", "未知灯效")
                return
            o = 0x3A + slot * 2
            b[9] = 1 if mode == 21 else 0
            b[10] = mode
            b[o] = max(0, min(9, self.bright.get()))
            b[o + 1] = ((max(0, min(4, self.speed.get())) << 4) | (self.colour.get() & 0x0F))
            sv = next((v for s, v in core.SLEEP_VALUES if s == self.sleep.get()), 10)
            b[0x18] = sv
            if self.dry.get():
                messagebox.showinfo("dry-run", "预览模式,未写入。\n灯效=%s 亮度=%d 速度=%d 颜色=%s" %
                                    (mode, b[o], b[o + 1] >> 4, core.COLORS[b[o + 1] & 0x0F]))
                return
            if use_rgb:
                r, g, bl = (int(rgb[i:i + 2], 16) for i in (0, 2, 4))
                plane = bytearray(126 * 3)
                for led in range(126):
                    plane[led] = r
                    plane[126 + led] = g
                    plane[252 + led] = bl
                self.dev.write("led_matrix", 0, bytes(plane))
                time.sleep(0.05)
                b[9] = 1
                b[10] = 21
            self.dev.write("led", 0, bytes(b[:128]))
            chk = core.read_led_block(self.dev)
            if chk[10] != b[10]:
                raise RuntimeError("写入后校验失败")
            messagebox.showinfo("完成", "灯效已写入键盘并校验通过。")
            self.refresh()
        except Exception as e:
            messagebox.showerror("失败", str(e))

    def turn_off(self):
        if not self.open_dev():
            return
        try:
            b = core.read_led_block(self.dev)
            slot = next(i for i, e in enumerate(core.EFFECTS) if e[0] == 0)
            o = 0x3A + slot * 2
            b[9] = 0
            b[10] = 0
            b[o] = 0
            b[o + 1] = 0
            if not self.dry.get():
                self.dev.write("led", 0, bytes(b[:128]))
            messagebox.showinfo("完成", "已关灯" + ("(dry-run)" if self.dry.get() else ""))
            self.refresh()
        except Exception as e:
            messagebox.showerror("失败", str(e))

    def apply_key(self):
        if not self.open_dev():
            return
        name = self.selected_key.get()
        idx = core.KEY_INDEX[name]
        try:
            new = core.parse_code(self.to.get())
        except Exception as e:
            messagebox.showerror("错误", str(e))
            return
        old = bytes(self.km[idx * 4:idx * 4 + 4])
        if self.dry.get():
            messagebox.showinfo("dry-run", "%s: %s -> %s (未写入)" %
                                (name, core.code_text(old), core.code_text(new)))
            return
        try:
            self.km = core.read_keymap(self.dev, 0)
            self.km[idx * 4:idx * 4 + 4] = new
            self.km[127 * 4:128 * 4] = core.SIGNATURE
            self.dev.write("keymap", 0, bytes(self.km))
            chk = core.read_keymap(self.dev, 0)
            if chk[idx * 4:idx * 4 + 4] != new:
                raise RuntimeError("写入后校验失败")
            messagebox.showinfo("完成", "%s 已改为 %s" % (name, core.code_text(new)))
            self.refresh()
        except Exception as e:
            messagebox.showerror("失败", str(e))

    def reset_key(self):
        name = self.selected_key.get()
        # 出厂码表:从 f75.py 的 USAGES/KEY_INDEX 推不出,直接读当前值仅占位提示
        messagebox.showinfo("提示", "长按 Fn+Esc 约 5 秒可把整把键盘恢复出厂设置。")

    def pick_rgb(self):
        c = colorchooser.askcolor(title="选择灯光颜色")
        if c and c[1]:
            self.rgb.set(c[1].lstrip("#").upper())

    def pick_backup(self):
        from tkinter import filedialog
        p = filedialog.asksaveasfilename(defaultextension=".bin",
                                         filetypes=[("备份文件", "*.bin"), ("所有文件", "*.*")])
        if p:
            self.backup_path.set(p)

    def do_backup(self):
        if not self.open_dev():
            return
        try:
            data = (core.read_keymap(self.dev, 0) + core.read_keymap(self.dev, 1) +
                    core.read_led_block(self.dev))
            with open(self.backup_path.get(), "wb") as f:
                f.write(data)
            self.lbl_backup.config(text="已备份 %d 字节到 %s" % (len(data), self.backup_path.get()))
        except Exception as e:
            messagebox.showerror("失败", str(e))

    def do_restore(self):
        if not self.open_dev():
            return
        p = self.restore_path.get()
        import os
        if not os.path.isfile(p):
            messagebox.showerror("错误", "文件不存在: " + p)
            return
        if not messagebox.askyesno("确认", "恢复会覆盖键盘当前全部配置,确定?"):
            return
        try:
            data = open(p, "rb").read()
            if len(data) != core.KEYMAP_LEN * 2 + core.LED_BLOCK_LEN:
                raise ValueError("备份文件大小不对")
            self.dev.write("keymap", 0, data[:core.KEYMAP_LEN])
            self.dev.write("keymap", 1, data[core.KEYMAP_LEN:core.KEYMAP_LEN * 2])
            self.dev.write("led", 0, data[core.KEYMAP_LEN * 2:])
            self.lbl_backup.config(text="已从 %s 恢复" % p)
            self.refresh()
        except Exception as e:
            messagebox.showerror("失败", str(e))


def main():
    if sys.platform != "win32":
        print("本工具仅支持 Windows。")
        return 1
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
