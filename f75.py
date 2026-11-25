#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""f75.py — AULA F75 键盘配置工具(零依赖单文件)

协议来自社区逆向(trenbolone1122/aula-f75-linux、rosman21/aula75-remapper、
vinicenter/aula-f75-webdriver)。仅在本机通过 Windows HID API 读写键盘板载配置,
不常驻后台、不联网、不写入注册表。

用法:  python f75.py <命令> [参数]
       python f75.py --help
"""

import os
import sys
import time
import ctypes
from ctypes import wintypes

# ============================================================ Win32 HID 绑定

class GUID(ctypes.Structure):
    _fields_ = [("Data1", wintypes.DWORD), ("Data2", wintypes.WORD),
                ("Data3", wintypes.WORD), ("Data4", ctypes.c_ubyte * 8)]

class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("InterfaceClassGuid", GUID),
                ("Flags", wintypes.DWORD), ("Reserved", ctypes.c_size_t)]

class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("Size", wintypes.ULONG), ("VendorID", wintypes.USHORT),
                ("ProductID", wintypes.USHORT), ("VersionNumber", wintypes.USHORT)]

class HIDP_CAPS(ctypes.Structure):
    _fields_ = [("Usage", wintypes.USHORT), ("UsagePage", wintypes.USHORT),
                ("InputReportByteLength", wintypes.USHORT),
                ("OutputReportByteLength", wintypes.USHORT),
                ("FeatureReportByteLength", wintypes.USHORT),
                ("Reserved", wintypes.USHORT * 17),
                ("NumberLinkCollectionNodes", wintypes.USHORT),
                ("NumberInputButtonCaps", wintypes.USHORT),
                ("NumberInputValueCaps", wintypes.USHORT),
                ("NumberInputDataIndices", wintypes.USHORT),
                ("NumberOutputButtonCaps", wintypes.USHORT),
                ("NumberOutputValueCaps", wintypes.USHORT),
                ("NumberOutputDataIndices", wintypes.USHORT),
                ("NumberFeatureButtonCaps", wintypes.USHORT),
                ("NumberFeatureValueCaps", wintypes.USHORT),
                ("NumberFeatureDataIndices", wintypes.USHORT)]

HID_GUID = GUID(0x4D1E55B2, 0xF16F, 0x11CF,
                (0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30))
INVALID_HANDLE = wintypes.HANDLE(-1).value
GENERIC_RW = 0x80000000 | 0x40000000
SHARE_ALL = 0x01 | 0x02
OPEN_EXISTING = 3

_setup = ctypes.WinDLL("setupapi.dll", use_last_error=True)
_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_hid = ctypes.WinDLL("hid.dll", use_last_error=True)

_setup.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), wintypes.LPCWSTR,
                                        wintypes.HWND, wintypes.DWORD]
_setup.SetupDiGetClassDevsW.restype = wintypes.HANDLE
_setup.SetupDiEnumDeviceInterfaces.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID), wintypes.DWORD,
    ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
_setup.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL
_setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [
    wintypes.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.c_void_p,
    wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
_setup.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOL
_setup.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]
_k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                             ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                             wintypes.HANDLE]
_k32.CreateFileW.restype = wintypes.HANDLE
_k32.CloseHandle.argtypes = [wintypes.HANDLE]
_k32.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                          ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
_k32.ReadFile.restype = wintypes.BOOL
_k32.WriteFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                           ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
_k32.WriteFile.restype = wintypes.BOOL
_hid.HidD_SetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
_hid.HidD_SetFeature.restype = wintypes.BOOL
_hid.HidD_GetFeature.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
_hid.HidD_GetFeature.restype = wintypes.BOOL
_hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
_hid.HidD_GetAttributes.restype = wintypes.BOOL
_hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
_hid.HidD_GetPreparsedData.restype = wintypes.BOOL
_hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
_hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)]
_hid.HidP_GetCaps.restype = ctypes.c_long

# ============================================================ 常量

WIRED_VIDPID = (0x258A, 0x010C)      # USB-C 线
RECEIVER_VIDPID = (0x3554, 0xFA09)   # 2.4G 接收器

WIRED = {"keymap": 0x03, "led_matrix": 0x06, "macros": 0x05,
         "led": 0x04, "read": 0x80, "reset": 0x11}
WIRELESS = {"keymap": 0x01, "led_matrix": 0x02, "macros": 0x03,
            "led": 0x04, "read": 0x40, "reset": 0x06, "online": 0x07}

LED_BLOCK_LEN = 128
KEYMAP_LEN = 512
MATRIX_LEN = 128
SIGNATURE = bytes([0x00, 0x00, 0x5A, 0xA5])

# 灯效 id -> 名称(协议顺序即槽位顺序)
EFFECTS = [
    (1, "静态常亮"), (2, "呼吸"), (3, "彩虹"), (4, "流光"),
    (5, "雨滴"), (6, "彩虹轮盘"), (7, "涟漪"), (8, "星光"),
    (9, "渐隐"), (10, "贪吃蛇"), (11, "霓虹"), (12, "触发"),
    (13, "正弦波"), (14, "扫描"), (15, "风车"), (16, "瀑布"),
    (17, "花开"), (18, "风暴"), (21, "自定义(逐键)"), (0, "关闭"),
]
COLORS = ["红", "绿", "蓝", "黄", "品红", "青", "白", "随机"]
SLEEP_VALUES = [(0, 0), (60, 2), (90, 3), (120, 4), (180, 6), (240, 8),
                (300, 10), (600, 20), (900, 30), (1200, 40)]

# 常用键的矩阵索引与出厂码(来自 F75 的 KB.ini / 社区逆向)
KEY_INDEX = {
    "Esc": 0, "F1": 12, "F2": 18, "F3": 24, "F4": 30, "F5": 36, "F6": 42,
    "F7": 48, "F8": 54, "F9": 60, "F10": 66, "F11": 72, "F12": 78,
    "`": 1, "1": 7, "2": 13, "3": 19, "4": 25, "5": 31, "6": 37,
    "7": 43, "8": 49, "9": 55, "0": 61, "-": 67, "=": 73,
    "Backspace": 79, "Del": 85, "Tab": 2, "Q": 8, "W": 14, "E": 20,
    "R": 26, "T": 32, "Y": 38, "U": 44, "I": 50, "O": 56, "P": 62,
    "[": 68, "]": 74, "\\": 80, "PgUp": 86, "Caps": 3, "A": 9, "S": 15,
    "D": 21, "F": 27, "G": 33, "H": 39, "J": 45, "K": 51, "L": 57,
    ";": 63, "'": 69, "Enter": 81, "PgDn": 87, "LShift": 4, "Z": 10,
    "X": 16, "C": 22, "V": 28, "B": 34, "N": 40, "M": 46, ",": 52,
    ".": 58, "/": 64, "RShift": 70, "Up": 82, "End": 88,
    "LCtrl": 5, "LWin": 11, "LAlt": 17, "Space": 35, "Fn": 53,
    "RCtrl": 59, "Left": 77, "Down": 83, "Right": 89, "Knob": 84,
}
MODS = {"LCtrl": 0x01, "LShift": 0x02, "LAlt": 0x04, "LWin": 0x08,
        "RCtrl": 0x10, "RShift": 0x20, "RAlt": 0x40, "RWin": 0x80}
USAGES = {}
for _c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    USAGES[_c] = 0x04 + ord(_c) - ord("A")
for _i, _c in enumerate("1234567890"):
    USAGES[_c] = 0x1E + _i
USAGES.update({"-": 0x2D, "=": 0x2E, "[": 0x2F, "]": 0x30, "\\": 0x31,
               ";": 0x33, "'": 0x34, "`": 0x35, ",": 0x36, ".": 0x37,
               "/": 0x38, "Space": 0x2C, "Enter": 0x28, "Esc": 0x29,
               "Backspace": 0x2A, "Tab": 0x2B, "Del": 0x4C,
               "Insert": 0x49, "Home": 0x4A, "PgUp": 0x4B, "End": 0x4D,
               "PgDn": 0x4E, "Left": 0x50, "Right": 0x4F, "Down": 0x51,
               "Up": 0x52, "Caps": 0x39, "PrtSc": 0x46, "ScrLk": 0x47,
               "Pause": 0x48, "Menu": 0x65})
for _i in range(1, 13):
    USAGES["F%d" % _i] = 0x39 + _i if _i < 10 else 0x67 + _i


def parse_code(text):
    """把 'LWin'、'LCtrl+C'、'raw:00:08:00:00'、'disabled' 解析成 4 字节码。"""
    t = text.strip()
    if t.lower() in ("disabled", "none", "off"):
        return bytes(4)
    if t.lower().startswith("raw:"):
        return bytes(int(x, 16) for x in t[4:].split(":"))
    mods = 0
    parts = t.split("+")
    for p in parts[:-1]:
        if p not in MODS:
            raise ValueError("未知修饰键: " + p)
        mods |= MODS[p]
    key = parts[-1]
    if key in USAGES:
        return bytes([0, mods, 0, USAGES[key]])
    if key in MODS:
        return bytes([0, MODS[key], 0, 0])
    raise ValueError("未知键名: " + key)


def code_text(code):
    if code == bytes(4):
        return "禁用"
    if code == SIGNATURE:
        return "<签名>"
    if code[0] == 0:
        mods = code[1]
        usage = code[3]
        names = [n for n, b in MODS.items() if mods & b]
        if usage == 0:
            return "+".join(names) if names else "禁用"
        keyname = next((k for k, v in USAGES.items() if v == usage), "0x%02x" % usage)
        return "+".join(names + [keyname]) if names else keyname
    if code[0] == 2:
        return "媒体:0x%04x" % ((code[2] << 8) | code[3])
    return code.hex(":")


# ============================================================ 设备发现

def enum_hid_paths():
    hdev = _setup.SetupDiGetClassDevsW(ctypes.byref(HID_GUID), None, None, 0x10 | 2)
    paths = []
    if hdev == INVALID_HANDLE:
        return paths
    i = 0
    try:
        while True:
            d = SP_DEVICE_INTERFACE_DATA()
            d.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)
            if not _setup.SetupDiEnumDeviceInterfaces(
                    hdev, None, ctypes.byref(HID_GUID), i, ctypes.byref(d)):
                break
            need = wintypes.DWORD()
            _setup.SetupDiGetDeviceInterfaceDetailW(
                hdev, ctypes.byref(d), None, 0, ctypes.byref(need), None)
            if need.value == 0:
                i += 1
                continue
            buf = ctypes.create_string_buffer(need.value)
            cb = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            ctypes.c_uint32.from_buffer(buf, 0).value = cb
            if _setup.SetupDiGetDeviceInterfaceDetailW(
                    hdev, ctypes.byref(d), buf, need.value, ctypes.byref(need), None):
                paths.append(ctypes.wstring_at(ctypes.addressof(buf) + 4))
            i += 1
    finally:
        _setup.SetupDiDestroyDeviceInfoList(hdev)
    return paths


def find_device(prefer=None):
    """返回 (路径, 类型, 命令表)。prefer: 'wired' / 'receiver' / None。"""
    paths = enum_hid_paths()
    found = {"wired": [], "receiver": []}
    for p in paths:
        u = p.upper()
        if "VID_258A&PID_010C" in u and "MI_01" in u and "COL06" in u:
            found["wired"].append(p)
        elif "VID_3554&PID_FA09" in u and "MI_01" in u:
            # 接收器的 vendor collection: 尝试打开并看 UsagePage
            fh = _k32.CreateFileW(p, GENERIC_RW, SHARE_ALL, None, OPEN_EXISTING, 0, 0)
            if fh != INVALID_HANDLE:
                try:
                    pds = ctypes.c_void_p()
                    if _hid.HidD_GetPreparsedData(fh, ctypes.byref(pds)):
                        caps = HIDP_CAPS()
                        if _hid.HidP_GetCaps(pds, ctypes.byref(caps)) == 0:
                            if caps.UsagePage == 0xFF02:
                                found["receiver"].append(p)
                        _hid.HidD_FreePreparsedData(pds)
                finally:
                    _k32.CloseHandle(fh)
    order = [prefer] if prefer in ("wired", "receiver") else ["wired", "receiver"]
    for kind in order:
        if found[kind]:
            return found[kind][0], kind, WIRED if kind == "wired" else WIRELESS
    return None, None, None


# ============================================================ 设备 I/O

class Device:
    def __init__(self, path, kind, cmds):
        self.path, self.kind, self.cmds = path, kind, cmds
        self.fh = _k32.CreateFileW(path, GENERIC_RW, SHARE_ALL, None, OPEN_EXISTING, 0, 0)
        if self.fh == INVALID_HANDLE:
            raise OSError("无法打开设备(错误 %d;可能需要管理员权限或键盘被占用)"
                          % ctypes.get_last_error())

    def close(self):
        if self.fh != INVALID_HANDLE:
            _k32.CloseHandle(self.fh)
            self.fh = INVALID_HANDLE

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _wired_xfer(self, cmd, sub, payload=None, size=0):
        total = max(1, (size + 511) // 512) if payload is None else max(1, (len(payload) + 511) // 512)
        out = bytearray()
        for idx in range(total):
            chunk_size = min(512, size - idx * 512) if payload is None \
                else min(512, len(payload) - idx * 512)
            buf = ctypes.create_string_buffer(520)
            buf[0] = 0x06
            buf[1] = cmd
            buf[2] = sub
            buf[4] = total
            buf[5] = idx
            buf[6] = chunk_size & 0xFF
            buf[7] = (chunk_size >> 8) & 0xFF
            if payload is not None:
                buf[8:8 + chunk_size] = payload[idx * 512:idx * 512 + chunk_size]
            if not _hid.HidD_SetFeature(self.fh, buf, 520):
                raise OSError("SetFeature 失败(错误 %d)" % ctypes.get_last_error())
            time.sleep(0.02)
            if payload is None:
                if not _hid.HidD_GetFeature(self.fh, buf, 520):
                    raise OSError("GetFeature 失败(错误 %d)" % ctypes.get_last_error())
                got = int.from_bytes(buf.raw[6:8], "little")
                out += buf.raw[8:8 + got]
        return bytes(out)

    def _wireless_pkt(self, cmd, sub, chunk, idx, total):
        p = bytearray(20)
        p[0] = 0x13
        p[1] = cmd
        p[2] = total
        p[3] = idx
        p[4] = ((sub & 0x0F) << 4) | len(chunk)
        p[5:5 + len(chunk)] = chunk
        p[19] = sum(p[:19]) & 0xFF
        return bytes(p)

    def _wireless_write(self, cmd, sub, payload):
        total = max(1, (len(payload) + 13) // 14)
        for idx in range(total):
            chunk = payload[idx * 14:(idx + 1) * 14]
            pkt = self._wireless_pkt(cmd, sub, chunk, idx, total)
            w = wintypes.DWORD()
            if not _k32.WriteFile(self.fh, ctypes.c_char_p(pkt), 20,
                                  ctypes.byref(w), None) or w.value != 20:
                raise OSError("WriteFile 失败(错误 %d)" % ctypes.get_last_error())
            time.sleep(0.015)
            # 等应答包(命令位相同、索引匹配)
            for _ in range(20):
                rb = ctypes.create_string_buffer(20)
                r = wintypes.DWORD()
                if _k32.ReadFile(self.fh, rb, 20, ctypes.byref(r), None) and r.value == 20:
                    d = rb.raw
                    if d[0] == 0x13 and (d[1] & 0x7F) == (cmd & 0x7F) and d[3] == idx:
                        break

    def _wireless_read(self, cmd, sub, size):
        p = bytearray(20)
        p[0] = 0x13
        p[1] = cmd | self.cmds["read"]
        p[2] = 1
        p[4] = (sub & 0x0F) << 4
        p[19] = sum(p[:19]) & 0xFF
        w = wintypes.DWORD()
        _k32.WriteFile(self.fh, ctypes.c_char_p(bytes(p)), 20, ctypes.byref(w), None)
        out = bytearray()
        total = None
        seen = set()
        for _ in range(200):
            rb = ctypes.create_string_buffer(20)
            r = wintypes.DWORD()
            if not _k32.ReadFile(self.fh, rb, 20, ctypes.byref(r), None) or r.value != 20:
                continue
            d = rb.raw
            if d[0] != 0x13 or (d[1] & 0x7F) != (cmd & 0x7F):
                continue
            if d[1] & 0x80 or d[2] & 0x80:
                continue
            idx = d[3]
            if idx in seen:
                continue
            seen.add(idx)
            total = d[2] & 0x7F
            n = d[4] & 0x0F
            out += d[5:5 + n]
            if idx + 1 >= total:
                break
        return bytes(out[:size])

    def read(self, what, sub=0, size=0):
        cmd = self.cmds[what]
        if self.kind == "wired":
            return self._wired_xfer(cmd | self.cmds["read"], sub, size=size)
        return self._wireless_read(cmd, sub, size)

    def write(self, what, sub, payload):
        cmd = self.cmds[what]
        if self.kind == "wired":
            return self._wired_xfer(cmd, sub, payload=payload)
        return self._wireless_write(cmd, sub, payload)

    def reset(self):
        if self.kind == "wired":
            self._wired_xfer(self.cmds["reset"], 0, payload=b"")
        else:
            self._wireless_write(self.cmds["reset"], 0, b"")


# ============================================================ 业务命令

def read_led_block(dev):
    b = dev.read("led", 0, LED_BLOCK_LEN)
    if len(b) < LED_BLOCK_LEN or b[126:128] != b"\x5A\xA5":
        raise RuntimeError("灯块校验失败(签名不匹配),未做任何修改")
    return bytearray(b)


def read_keymap(dev, layer=0):
    km = dev.read("keymap", layer, KEYMAP_LEN)
    if len(km) < KEYMAP_LEN:
        raise RuntimeError("键位读取不完整(%d/%d 字节)" % (len(km), KEYMAP_LEN))
    if km[:4] != bytes([0, 0, 0, 0x29]):
        raise RuntimeError("键位布局校验失败(Esc 不是 0x29),可能不是 F75 或读取错位")
    return bytearray(km)


def led_info(b):
    mode = b[10]
    slot = next((i for i, e in enumerate(EFFECTS) if e[0] == mode), None)
    name = next((e[1] for e in EFFECTS if e[0] == mode), "未知(%d)" % mode)
    if slot is None:
        return name, None, None, None
    o = 0x3A + slot * 2
    bright = min(b[o], 9)
    speed = min(b[o + 1] >> 4, 4)
    colour = min(b[o + 1] & 0x0F, 7)
    return name, bright, speed, COLORS[colour]


def cmd_info(args):
    path, kind, cmds = find_device(args.link)
    if not path:
        print("未找到 F75。请插上 USB-C 线或 2.4G 接收器,再试。")
        return 1
    with Device(path, kind, cmds) as dev:
        vidpid = WIRED_VIDPID if kind == "wired" else RECEIVER_VIDPID
        print("设备: %s (%04X:%04X)" % (
            "USB-C 有线" if kind == "wired" else "2.4G 接收器", vidpid[0], vidpid[1]))
        try:
            b = read_led_block(dev)
            name, bright, speed, colour = led_info(b)
            print("灯效: %s" % name)
            if bright is not None:
                print("亮度: %d/9   速度: %d/4   颜色: %s" % (bright, speed, colour))
            sv = next((s for s, v in SLEEP_VALUES if v == b[0x18]), None)
            print("休眠: %s" % ("关闭" if sv == 0 else
                              ("%d 秒" % sv if sv else "未知(0x%02x)" % b[0x18])))
        except Exception as e:
            print("灯效读取失败:", e)
        try:
            km = read_keymap(dev)
            print("键位: Win=%s  Alt=%s  Caps=%s" % (
                code_text(km[11 * 4:11 * 4 + 4]),
                code_text(km[17 * 4:17 * 4 + 4]),
                code_text(km[3 * 4:3 * 4 + 4])))
        except Exception as e:
            print("键位读取失败:", e)
    return 0


def cmd_light(args):
    if args.effect is None and args.rgb is None:
        print("请指定 --effect 或 --rgb。用 'python f75.py effects' 查看灯效列表。")
        return 1
    path, kind, cmds = find_device(args.link)
    if not path:
        print("未找到 F75。请插上 USB-C 线或 2.4G 接收器,再试。")
        return 1
    with Device(path, kind, cmds) as dev:
        b = read_led_block(dev)
        old_name, _, _, _ = led_info(b)
        print("当前灯效: %s" % old_name)

        rgb = None
        if args.rgb:
            try:
                rgb = tuple(int(args.rgb[i:i + 2], 16) for i in (0, 2, 4))
            except ValueError:
                print("--rgb 格式应为 6 位十六进制,如 FF0000")
                return 1

        if rgb:
            # 逐键自定义色:写 LED 矩阵 3 个 126 字节平面,再切到自定义模式
            mode = 21
            plane = bytearray(126 * 3)
            for led in range(126):
                plane[led] = rgb[0]
                plane[126 + led] = rgb[1]
                plane[252 + led] = rgb[2]
            if not args.dry_run:
                dev.write("led_matrix", 0, bytes(plane))
                time.sleep(0.05)
        else:
            mode = args.effect

        slot = next((i for i, e in enumerate(EFFECTS) if e[0] == mode), None)
        if slot is None:
            print("未知灯效 id: %d。用 'python f75.py effects' 查看列表。" % mode)
            return 1
        o = 0x3A + slot * 2
        brightness = args.brightness if args.brightness is not None else min(b[o], 9)
        speed = args.speed if args.speed is not None else min(b[o + 1] >> 4, 4)
        colour = args.colour if args.colour is not None else (min(b[o + 1] & 0x0F, 7) if rgb is None else 0)

        b[9] = 1 if mode == 21 else 0
        b[10] = mode
        b[o] = max(0, min(9, brightness))
        b[o + 1] = ((max(0, min(4, speed)) << 4) | (colour & 0x0F))
        if args.sleep is not None:
            sv = next((v for s, v in SLEEP_VALUES if s == args.sleep), None)
            if sv is None:
                print("--sleep 只接受: " + ", ".join(str(s) for s, _ in SLEEP_VALUES))
                return 1
            b[0x18] = sv

        name = next(e[1] for e in EFFECTS if e[0] == mode)
        print("新灯效: %s  亮度 %d  速度 %d  颜色 %s" % (
            name, b[o], b[o + 1] >> 4, COLORS[b[o + 1] & 0x0F]))
        if args.dry_run:
            print("[dry-run] 未写入")
            return 0
        dev.write("led", 0, bytes(b[:LED_BLOCK_LEN]))
        check = read_led_block(dev)
        if check[10] != mode:
            raise RuntimeError("写入后校验失败: 灯效 id 不匹配")
        print("已写入键盘并校验通过(断电/换电脑后仍保持)。")
    return 0


def cmd_key(args):
    path, kind, cmds = find_device(args.link)
    if not path:
        print("未找到 F75。请插上 USB-C 线或 2.4G 接收器,再试。")
        return 1
    with Device(path, kind, cmds) as dev:
        layer = args.layer
        km = read_keymap(dev, layer)
        if args.list:
            for name, idx in KEY_INDEX.items():
                c = km[idx * 4:idx * 4 + 4]
                print("%-10s idx=%3d  %s" % (name, idx, code_text(c)))
            return 0
        if not args.key:
            print("请用 --list 查看,或用 --key 指定要改的键。")
            return 1
        kn = args.key
        if kn not in KEY_INDEX:
            print("未知键名: %s。用 'python f75.py keylist' 看支持的键名。" % kn)
            return 1
        idx = KEY_INDEX[kn]
        old = bytes(km[idx * 4:idx * 4 + 4])
        new = parse_code(args.to)
        print("%s (idx=%d): %s -> %s" % (kn, idx, code_text(old), code_text(new)))
        if args.dry_run:
            print("[dry-run] 未写入")
            return 0
        km[idx * 4:idx * 4 + 4] = new
        # 写回时最后一个槽必须带 5A A5 签名,否则重启后丢失
        km[127 * 4:128 * 4] = SIGNATURE
        dev.write("keymap", layer, bytes(km))
        check = read_keymap(dev, layer)
        if check[idx * 4:idx * 4 + 4] != new:
            raise RuntimeError("写入后校验失败: 键位不匹配")
        print("已写入键盘并校验通过。")
    return 0


def cmd_dump(args):
    path, kind, cmds = find_device(args.link)
    if not path:
        print("未找到 F75。")
        return 1
    out = args.out or "f75-backup.bin"
    with Device(path, kind, cmds) as dev:
        km0 = read_keymap(dev, 0)
        km1 = read_keymap(dev, 1)
        led = read_led_block(dev)
    with open(out, "wb") as f:
        f.write(km0 + km1 + led)
    print("已备份到 %s (%d 字节)" % (out, len(km0) + len(km1) + len(led)))
    return 0


def cmd_restore(args):
    if not os.path.isfile(args.input):
        print("备份文件不存在:", args.input)
        return 1
    data = open(args.input, "rb").read()
    if len(data) != KEYMAP_LEN * 2 + LED_BLOCK_LEN:
        print("备份文件大小不对(%d 字节)" % len(data))
        return 1
    path, kind, cmds = find_device(args.link)
    if not path:
        print("未找到 F75。")
        return 1
    with Device(path, kind, cmds) as dev:
        dev.write("keymap", 0, data[:KEYMAP_LEN])
        dev.write("keymap", 1, data[KEYMAP_LEN:KEYMAP_LEN * 2])
        dev.write("led", 0, data[KEYMAP_LEN * 2:])
    print("已从 %s 恢复。" % args.input)
    return 0


def cmd_effects(args):
    print("灯效 id  名称")
    for i, n in EFFECTS:
        print("  %2d     %s" % (i, n))
    print("\n颜色: " + ", ".join("%d=%s" % (i, c) for i, c in enumerate(COLORS)))
    print("休眠秒数: " + ", ".join(str(s) for s, _ in SLEEP_VALUES) + " (0=关闭)")
    return 0


def cmd_keylist(args):
    print("支持的键名(区分大小写):")
    print("  " + "  ".join(sorted(KEY_INDEX)))
    print("\n--to 写法: LWin / LCtrl+C / raw:00:08:00:00 / disabled")
    return 0


# ============================================================ CLI

def build_parser():
    import argparse
    p = argparse.ArgumentParser(
        prog="f75.py",
        description="AULA F75 键盘配置工具(零依赖单文件;配置写入键盘,无需后台运行)")
    p.add_argument("--link", choices=["wired", "receiver"], default=None,
                   help="指定连接方式(默认自动: 优先 USB-C,其次 2.4G)")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="查看设备与当前配置").set_defaults(func=cmd_info)
    sub.add_parser("effects", help="列出灯效/颜色/休眠取值").set_defaults(func=cmd_effects)
    sub.add_parser("keylist", help="列出可改的键名").set_defaults(func=cmd_keylist)

    pl = sub.add_parser("light", help="修改灯光")
    pl.add_argument("--effect", type=int, help="灯效 id(见 effects)")
    pl.add_argument("--rgb", help="逐键自定义色,6 位十六进制如 FF0000")
    pl.add_argument("--brightness", type=int, help="亮度 0-9")
    pl.add_argument("--speed", type=int, help="速度 0-4")
    pl.add_argument("--colour", type=int, help="颜色 0-7(见 effects)")
    pl.add_argument("--sleep", type=int, help="休眠秒数 0/60/120/300/600/1200…")
    pl.add_argument("--dry-run", action="store_true", help="只显示不写入")
    pl.set_defaults(func=cmd_light)

    pk = sub.add_parser("key", help="修改键位映射")
    pk.add_argument("--layer", type=int, choices=[0, 1], default=0,
                    help="0=基础层(默认),1=Fn 层")
    pk.add_argument("--list", action="store_true", help="列出当前键位")
    pk.add_argument("--key", help="要修改的键名(见 keylist)")
    pk.add_argument("--to", help="目标: LWin / LCtrl+C / raw:00:08:00:00 / disabled")
    pk.add_argument("--dry-run", action="store_true", help="只显示不写入")
    pk.set_defaults(func=cmd_key)

    pd = sub.add_parser("dump", help="备份键位与灯光到文件")
    pd.add_argument("--out", help="输出文件(默认 f75-backup.bin)")
    pd.set_defaults(func=cmd_dump)

    pr = sub.add_parser("restore", help="从备份恢复")
    pr.add_argument("input", help="备份文件路径")
    pr.set_defaults(func=cmd_restore)

    return p


def main():
    if sys.platform != "win32":
        print("本工具仅支持 Windows(使用 Win32 HID API)。")
        return 1
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except KeyboardInterrupt:
        print("\n已取消。")
        return 130
    except Exception as e:
        print("错误:", e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
