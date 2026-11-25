/*
 * f75.c — AULA F75 键盘配置工具 (原生 C, 零第三方依赖)
 *
 * 协议来自社区逆向 (trenbolone1122 / rosman21 / vinicenter)。
 * 仅通过 Windows HID API 读写键盘板载配置, 不常驻后台、不联网。
 *
 * 编译 (本机 MSVC):
 *   cl /nologo /O2 /utf-8 /Fe:f75.exe f75.c
 * 编译 (GitHub Actions 见 .github/workflows/build.yml)
 *
 * 用法:
 *   f75.exe info
 *   f75.exe light --effect 1 --brightness 7 --speed 0 --colour 0
 *   f75.exe light --rgb FF0000
 *   f75.exe key --list
 *   f75.exe key --key LWin --to LWin
 *   f75.exe dump --out backup.bin
 *   f75.exe restore backup.bin
 */
#define _WIN32_WINNT 0x0600
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "kernel32.lib")

/* ------------------------------------------------------------ 常量 */
#define VID_WIRED 0x258A
#define PID_WIRED 0x010C
#define VID_RX    0x3554
#define PID_RX    0xFA09

#define CMD_WIRED_KEYMAP 0x03
#define CMD_WIRED_LED    0x04
#define CMD_WIRED_MATRIX 0x06
#define CMD_RX_KEYMAP    0x01
#define CMD_RX_LED       0x04
#define CMD_RX_MATRIX    0x02

#define LED_LEN    128
#define KEY_LEN    512
#define MATRIX_LEN 128
#define REPORT_LEN 520
#define PKT_LEN    20
#define CHUNK      14

typedef struct { int kind; HANDLE h; } Dev;   /* kind: 0=wired 1=receiver */

typedef struct { int id; const char *name; int has_speed; int has_colour; } Effect;
static const Effect EFFECTS[] = {
    {1,"静态常亮",0,1},{2,"呼吸",1,1},{3,"彩虹",1,0},{4,"流光",1,1},
    {5,"雨滴",1,1},{6,"彩虹轮盘",1,1},{7,"涟漪",1,1},{8,"星光",1,1},
    {9,"渐隐",1,1},{10,"贪吃蛇",1,1},{11,"霓虹",1,1},{12,"触发",1,1},
    {13,"正弦波",1,1},{14,"扫描",1,1},{15,"风车",1,0},{16,"瀑布",1,0},
    {17,"花开",1,0},{18,"风暴",1,1},{21,"逐键自定义",0,0},{0,"关闭",0,0}
};
#define NEFFECTS ((int)(sizeof(EFFECTS)/sizeof(EFFECTS[0])))
static const char *COLORS[] = {"红","绿","蓝","黄","品红","青","白","随机"};
static const struct { int sec; unsigned char val; } SLEEP[] = {
    {0,0},{60,2},{90,3},{120,4},{180,6},{240,8},{300,10},{600,20},{900,30},{1200,40}
};

typedef struct { const char *name; int idx; } KeyDef;
static const KeyDef KEYS[] = {
    {"Esc",0},{"F1",12},{"F2",18},{"F3",24},{"F4",30},{"F5",36},{"F6",42},
    {"F7",48},{"F8",54},{"F9",60},{"F10",66},{"F11",72},{"F12",78},{"Knob",84},
    {"`",1},{"1",7},{"2",13},{"3",19},{"4",25},{"5",31},{"6",37},{"7",43},
    {"8",49},{"9",55},{"0",61},{"-",67},{"=",73},{"Backspace",79},{"Del",85},
    {"Tab",2},{"Q",8},{"W",14},{"E",20},{"R",26},{"T",32},{"Y",38},{"U",44},
    {"I",50},{"O",56},{"P",62},{"[",68},{"]",74},{"\\",80},{"PgUp",86},
    {"Caps",3},{"A",9},{"S",15},{"D",21},{"F",27},{"G",33},{"H",39},{"J",45},
    {"K",51},{"L",57},{";",63},{"'",69},{"Enter",81},{"PgDn",87},
    {"LShift",4},{"Z",10},{"X",16},{"C",22},{"V",28},{"B",34},{"N",40},{"M",46},
    {",",52},{".",58},{"/",64},{"RShift",70},{"Up",82},{"End",88},
    {"LCtrl",5},{"LWin",11},{"LAlt",17},{"Space",35},{"Fn",53},{"RCtrl",59},
    {"Left",77},{"Down",83},{"Right",89}
};
#define NKEYS ((int)(sizeof(KEYS)/sizeof(KEYS[0])))

static const struct { const char *name; unsigned char bit; } MODS[] = {
    {"LCtrl",0x01},{"LShift",0x02},{"LAlt",0x04},{"LWin",0x08},
    {"RCtrl",0x10},{"RShift",0x20},{"RAlt",0x40},{"RWin",0x80}
};
static const struct { const char *name; unsigned char usage; } USAGES[] = {
    {"A",0x04},{"B",0x05},{"C",0x06},{"D",0x07},{"E",0x08},{"F",0x09},{"G",0x0a},
    {"H",0x0b},{"I",0x0c},{"J",0x0d},{"K",0x0e},{"L",0x0f},{"M",0x10},{"N",0x11},
    {"O",0x12},{"P",0x13},{"Q",0x14},{"R",0x15},{"S",0x16},{"T",0x17},{"U",0x18},
    {"V",0x19},{"W",0x1a},{"X",0x1b},{"Y",0x1c},{"Z",0x1d},
    {"1",0x1e},{"2",0x1f},{"3",0x20},{"4",0x21},{"5",0x22},{"6",0x23},{"7",0x24},
    {"8",0x25},{"9",0x26},{"0",0x27},
    {"-",0x2d},{"=",0x2e},{"[",0x2f},{"]",0x30},{"\\",0x31},{";",0x33},{"'",0x34},
    {"`",0x35},{",",0x36},{".",0x37},{"/",0x38},{"Space",0x2c},{"Enter",0x28},
    {"Esc",0x29},{"Backspace",0x2a},{"Tab",0x2b},{"Del",0x4c},{"Insert",0x49},
    {"Home",0x4a},{"PgUp",0x4b},{"End",0x4d},{"PgDn",0x4e},{"Left",0x50},
    {"Right",0x4f},{"Down",0x51},{"Up",0x52},{"Caps",0x39},{"PrtSc",0x46},
    {"ScrLk",0x47},{"Pause",0x48},{"Menu",0x65}
};

/* ------------------------------------------------------------ 工具 */
static int min_i(int a, int b) { return a < b ? a : b; }

static unsigned char checksum(const unsigned char *p) {
    unsigned int s = 0;
    for (int i = 0; i < PKT_LEN - 1; i++) s += p[i];
    return (unsigned char)(s & 0xff);
}

static int key_index(const char *name) {
    for (int i = 0; i < NKEYS; i++) if (!strcmp(KEYS[i].name, name)) return KEYS[i].idx;
    return -1;
}

static int usage_of(const char *name) {
    for (unsigned i = 0; i < sizeof(USAGES)/sizeof(USAGES[0]); i++)
        if (!strcmp(USAGES[i].name, name)) return USAGES[i].usage;
    if (!strncmp(name, "F", 1) && strlen(name) <= 3) {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 9) return 0x39 + n;
        if (n >= 10 && n <= 12) return 0x67 + n;
    }
    return -1;
}

static int mod_of(const char *name) {
    for (unsigned i = 0; i < sizeof(MODS)/sizeof(MODS[0]); i++)
        if (!strcmp(MODS[i].name, name)) return MODS[i].bit;
    return -1;
}

/* 把 "LWin" / "LCtrl+C" / "raw:00:08:00:00" / "disabled" 解析成 4 字节码 */
static int parse_code(const char *text, unsigned char *out) {
    char buf[128];
    strncpy_s(buf, sizeof(buf), text, _TRUNCATE);
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);
    memset(out, 0, 4);
    if (!strcmp(buf, "disabled") || !strcmp(buf, "none") || !strcmp(buf, "off")) return 0;
    if (!strncmp(buf, "raw:", 4)) {
        int v[4];
        if (sscanf_s(buf + 4, "%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3]) != 4) return -1;
        for (int i = 0; i < 4; i++) out[i] = (unsigned char)v[i];
        return 0;
    }
    char *parts[8]; int n = 0;
    parts[n++] = buf;
    for (char *p = buf; *p; p++) if (*p == '+') { *p = 0; parts[n++] = p + 1; }
    unsigned char mods = 0;
    for (int i = 0; i < n - 1; i++) {
        int m = mod_of(parts[i]);
        if (m < 0) return -1;
        mods |= (unsigned char)m;
    }
    const char *k = parts[n - 1];
    int u = usage_of(k);
    if (u >= 0) { out[1] = mods; out[3] = (unsigned char)u; return 0; }
    int m = mod_of(k);
    if (m >= 0) { out[1] = (unsigned char)m; return 0; }
    return -1;
}

static const char *code_text(const unsigned char *c) {
    static char b[96];
    if (!c[0] && !c[1] && !c[2] && !c[3]) return "禁用";
    if (c[0] == 0) {
        char m[48] = "";
        for (unsigned i = 0; i < sizeof(MODS)/sizeof(MODS[0]); i++)
            if (c[1] & MODS[i].bit) {
                if (*m) strcat_s(m, sizeof(m), "+");
                strcat_s(m, sizeof(m), MODS[i].name);
            }
        if (c[3] == 0) {
            if (*m) { strcpy_s(b, sizeof(b), m); return b; }
            return "禁用";
        }
        const char *u = "?";
        for (unsigned i = 0; i < sizeof(USAGES)/sizeof(USAGES[0]); i++)
            if (USAGES[i].usage == c[3]) { u = USAGES[i].name; break; }
        if (!strcmp(u, "?")) {
            sprintf_s(b, sizeof(b), "0x%02x", c[3]);
            return b;
        }
        if (*m) sprintf_s(b, sizeof(b), "%s+%s", m, u);
        else strcpy_s(b, sizeof(b), u);
        return b;
    }
    sprintf_s(b, sizeof(b), "%02x:%02x:%02x:%02x", c[0], c[1], c[2], c[3]);
    return b;
}

/* ------------------------------------------------------------ 设备枚举 */
static const GUID HID_GUID = {
    0x4D1E55B2,0xF16F,0x11CF,{0x88,0xCB,0x00,0x11,0x11,0x00,0x00,0x30}
};

static int find_device(Dev *d) {
    HDEVINFO hdev = SetupDiGetClassDevsW(&HID_GUID, NULL, NULL,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdev == INVALID_HANDLE_VALUE) return -1;
    int result = -1;
    SP_DEVICE_INTERFACE_DATA did;
    did.cbSize = sizeof(did);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hdev, NULL, &HID_GUID, i, &did); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(hdev, &did, NULL, 0, &need, NULL);
        if (!need) continue;
        unsigned char *buf = (unsigned char *)malloc(need);
        if (!buf) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *det = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)buf;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(hdev, &did, det, need, &need, NULL)) {
            free(buf); continue;
        }
        WCHAR up[512];
        for (int j = 0; det->DevicePath[j]; j++) up[j] = (WCHAR)towupper(det->DevicePath[j]);
        up[511] = 0;
        int kind = -1;
        if (wcsstr(up, L"VID_258A&PID_010C") && wcsstr(up, L"MI_01") && wcsstr(up, L"COL06"))
            kind = 0;
        else if (wcsstr(up, L"VID_3554&PID_FA09") && wcsstr(up, L"MI_01"))
            kind = 1;
        if (kind >= 0) {
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                if (kind == 1) {
                    /* 接收器: 确认 vendor usage page 0xFF02 */
                    PHIDP_PREPARSED_DATA pds = NULL;
                    if (HidD_GetPreparsedData(h, &pds)) {
                        HIDP_CAPS caps;
                        if (HidP_GetCaps(pds, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 0xFF02)
                            result = kind;
                        HidD_FreePreparsedData(pds);
                    }
                    if (result != kind) CloseHandle(h);
                    else d->h = h;
                } else {
                    d->h = h; result = kind;
                }
            }
        }
        free(buf);
        if (result >= 0) break;
    }
    SetupDiDestroyDeviceInfoList(hdev);
    d->kind = result;
    return result;
}

/* ------------------------------------------------------------ HID I/O */
static int wired_xfer(Dev *d, unsigned char cmd, unsigned char sub,
                      const unsigned char *payload, DWORD len, unsigned char *out) {
    int total = (int)((len + 511) / 512);
    if (total < 1) total = 1;
    for (int idx = 0; idx < total; idx++) {
        DWORD chunk = min_i(512, (int)len - idx * 512);
        unsigned char buf[REPORT_LEN];
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x06; buf[1] = cmd; buf[2] = sub;
        buf[4] = (unsigned char)total; buf[5] = (unsigned char)idx;
        buf[6] = (unsigned char)(chunk & 0xff);
        buf[7] = (unsigned char)((chunk >> 8) & 0xff);
        if (payload) memcpy(buf + 8, payload + idx * 512, chunk);
        if (!HidD_SetFeature(d->h, buf, REPORT_LEN)) return -1;
        Sleep(24);
        if (out && !payload) {
            if (!HidD_GetFeature(d->h, buf, REPORT_LEN)) return -1;
            DWORD got = (DWORD)buf[6] | ((DWORD)buf[7] << 8);
            memcpy(out + idx * 512, buf + 8, min_i((int)got, (int)chunk));
        }
    }
    return 0;
}

static int wireless_write(Dev *d, unsigned char cmd, unsigned char sub,
                          const unsigned char *payload, DWORD len) {
    int total = (int)((len + CHUNK - 1) / CHUNK);
    if (total < 1) total = 1;
    for (int idx = 0; idx < total; idx++) {
        int n = min_i(CHUNK, (int)len - idx * CHUNK);
        unsigned char p[PKT_LEN];
        memset(p, 0, sizeof(p));
        p[0] = 0x13; p[1] = cmd; p[2] = (unsigned char)total; p[3] = (unsigned char)idx;
        p[4] = (unsigned char)(((sub & 0x0f) << 4) | n);
        memcpy(p + 5, payload + idx * CHUNK, n);
        p[19] = checksum(p);
        DWORD w = 0;
        if (!WriteFile(d->h, p, PKT_LEN, &w, NULL) || w != PKT_LEN) return -1;
        Sleep(16);
    }
    return 0;
}

static int wireless_read(Dev *d, unsigned char cmd, unsigned char sub,
                         unsigned char *out, DWORD size) {
    unsigned char req[PKT_LEN];
    memset(req, 0, sizeof(req));
    req[0] = 0x13; req[1] = (unsigned char)(cmd | 0x40); req[2] = 1;
    req[4] = (unsigned char)((sub & 0x0f) << 4);
    req[19] = checksum(req);
    DWORD w = 0;
    WriteFile(d->h, req, PKT_LEN, &w, NULL);
    unsigned char seen[256]; memset(seen, 0, sizeof(seen));
    int total = 0, got = 0;
    for (int n = 0; n < 200; n++) {
        unsigned char r[PKT_LEN]; DWORD rd = 0;
        if (!ReadFile(d->h, r, PKT_LEN, &rd, NULL) || rd != PKT_LEN) continue;
        if (r[0] != 0x13) continue;
        if ((r[1] & 0x7f) != (cmd & 0x7f)) continue;
        if (r[1] & 0x80 || r[2] & 0x80) continue;
        int idx = r[3];
        if (seen[idx]) continue;
        seen[idx] = 1;
        total = r[2] & 0x7f;
        int nbytes = r[4] & 0x0f;
        if (got + nbytes <= (int)size) {
            memcpy(out + got, r + 5, nbytes);
            got += nbytes;
        }
        if (total && got >= total * CHUNK) break;
    }
    return 0;
}

static int dev_read(Dev *d, int what, unsigned char sub, unsigned char *out, DWORD size) {
    if (d->kind == 0) {
        unsigned char cmd = what == 0 ? CMD_WIRED_KEYMAP : CMD_WIRED_LED;
        return wired_xfer(d, (unsigned char)(cmd | 0x80), sub, NULL, size, out);
    }
    unsigned char cmd = what == 0 ? CMD_RX_KEYMAP : CMD_RX_LED;
    return wireless_read(d, cmd, sub, out, size);
}

static int dev_write(Dev *d, int what, unsigned char sub, const unsigned char *data, DWORD len) {
    if (d->kind == 0) {
        unsigned char cmd = what == 0 ? CMD_WIRED_KEYMAP :
                            (what == 1 ? CMD_WIRED_LED : CMD_WIRED_MATRIX);
        return wired_xfer(d, cmd, sub, data, len, NULL);
    }
    unsigned char cmd = what == 0 ? CMD_RX_KEYMAP :
                        (what == 1 ? CMD_RX_LED : CMD_RX_MATRIX);
    return wireless_write(d, cmd, sub, data, len);
}

/* ------------------------------------------------------------ 业务 */
static int read_led(Dev *d, unsigned char *b) {
    memset(b, 0, LED_LEN);
    if (dev_read(d, 1, 0, b, LED_LEN)) return -1;
    if (b[126] != 0x5a || b[127] != 0xa5) return -2;
    return 0;
}
static int read_keymap(Dev *d, unsigned char layer, unsigned char *km) {
    memset(km, 0, KEY_LEN);
    if (dev_read(d, 0, layer, km, KEY_LEN)) return -1;
    if (km[0] != 0 || km[3] != 0x29) return -2;   /* Esc 应为 00 00 00 29 */
    return 0;
}

static int effect_slot(int id) {
    for (int i = 0; i < NEFFECTS; i++) if (EFFECTS[i].id == id) return i;
    return -1;
}
static void edit_led(unsigned char *b, int mode, int bright, int speed, int colour) {
    int slot = effect_slot(mode);
    b[9] = (unsigned char)(mode == 21 ? 1 : 0);
    b[10] = (unsigned char)mode;
    if (slot >= 0) {
        if (bright >= 0) b[0x3a + slot * 2] = (unsigned char)min_i(9, bright);
        if (speed >= 0 || colour >= 0) {
            int sp = speed >= 0 ? min_i(4, speed) : (b[0x3b + slot * 2] >> 4);
            int co = colour >= 0 ? min_i(7, colour) : (b[0x3b + slot * 2] & 0x0f);
            b[0x3b + slot * 2] = (unsigned char)((sp << 4) | co);
        }
    }
}
static void led_info(const unsigned char *b, int *mode, int *bright, int *speed, int *colour) {
    *mode = b[10];
    int slot = effect_slot(*mode);
    if (slot < 0) { *bright = *speed = *colour = 0; return; }
    *bright = min_i(9, b[0x3a + slot * 2]);
    *speed  = min_i(4, b[0x3b + slot * 2] >> 4);
    *colour = min_i(7, b[0x3b + slot * 2] & 0x0f);
}
static const char *effect_name(int id) {
    int slot = effect_slot(id);
    return slot >= 0 ? EFFECTS[slot].name : "未知";
}

/* ------------------------------------------------------------ 命令 */
static int ensure_device(Dev *d) {
    if (find_device(d) < 0) {
        printf("未找到 F75。请插上 USB-C 线或 2.4G 接收器, 并关闭官方驱动。\n");
        return -1;
    }
    printf("设备: %s\n", d->kind == 0 ? "USB-C 有线 (258A:010C)" : "2.4G 接收器 (3554:FA09)");
    return 0;
}

static int cmd_info(void) {
    Dev d; if (ensure_device(&d)) return 1;
    unsigned char led[LED_LEN], km[KEY_LEN];
    int rc = read_led(&d, led);
    if (rc) printf("灯效读取失败 (签名错误)\n");
    else {
        int m, br, sp, co; led_info(led, &m, &br, &sp, &co);
        printf("灯效: %s (ID %d)\n", effect_name(m), m);
        printf("亮度: %d/9   速度: %d/4   颜色: %s\n", br, sp, COLORS[co]);
        int sec = -1;
        for (unsigned i = 0; i < sizeof(SLEEP)/sizeof(SLEEP[0]); i++)
            if (SLEEP[i].val == led[0x18]) sec = SLEEP[i].sec;
        if (sec == 0) printf("休眠: 关闭\n");
        else if (sec > 0) printf("休眠: %d 秒\n", sec);
        else printf("休眠: 未知 (0x%02x)\n", led[0x18]);
    }
    rc = read_keymap(&d, 0, km);
    if (rc) printf("键位读取失败 (布局校验错误)\n");
    else {
        const char *names[] = {"LWin","LAlt","Caps","Space","Fn"};
        int idxs[] = {11,17,3,35,53};
        printf("键位: ");
        for (int i = 0; i < 5; i++)
            printf("%s=%s%s", names[i], code_text(km + idxs[i] * 4), i < 4 ? "  " : "\n");
    }
    CloseHandle(d.h);
    return 0;
}

static int cmd_light(int argc, char **argv) {
    int effect = -1, bright = -1, speed = -1, colour = -1, rgb = -1;
    unsigned char r = 255, g = 0, b = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--effect") && i + 1 < argc) effect = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--brightness") && i + 1 < argc) bright = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--colour") && i + 1 < argc) colour = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rgb") && i + 1 < argc) {
            int ri, gi, bi;
            if (sscanf_s(argv[++i], "%2x%2x%2x", &ri, &gi, &bi) == 3) {
                r = (unsigned char)ri; g = (unsigned char)gi; b = (unsigned char)bi; rgb = 1;
            }
        } else { printf("未知参数: %s\n", argv[i]); return 1; }
    }
    if (effect < 0 && rgb < 0) {
        printf("请指定 --effect N 或 --rgb RRGGBB\n");
        return 1;
    }
    Dev d; if (ensure_device(&d)) return 1;
    unsigned char led[LED_LEN];
    if (read_led(&d, led)) { printf("灯块校验失败, 未修改。\n"); CloseHandle(d.h); return 1; }
    int oldm, br, sp, co; led_info(led, &oldm, &br, &sp, &co);
    printf("当前灯效: %s\n", effect_name(oldm));
    if (rgb) {
        unsigned char plane[MATRIX_LEN * 3];
        memset(plane, 0, sizeof(plane));
        for (int i = 0; i < 126; i++) {
            plane[i] = r; plane[126 + i] = g; plane[252 + i] = b;
        }
        if (dev_write(&d, 2, 0, plane, sizeof(plane))) { printf("逐键灯色写入失败\n"); }
        effect = 21;
    }
    edit_led(led, effect, bright, speed, colour);
    if (dev_write(&d, 1, 0, led, LED_LEN)) { printf("灯块写入失败\n"); CloseHandle(d.h); return 1; }
    unsigned char chk[LED_LEN];
    if (read_led(&d, chk) || chk[10] != led[10]) {
        printf("写入后校验失败!\n"); CloseHandle(d.h); return 1;
    }
    int nm, nb, ns, nc; led_info(chk, &nm, &nb, &ns, &nc);
    printf("已写入: %s  亮度 %d  速度 %d  颜色 %s\n", effect_name(nm), nb, ns, COLORS[nc]);
    CloseHandle(d.h);
    return 0;
}

static int cmd_key(int argc, char **argv) {
    int layer = 0, list = 0;
    const char *keyname = NULL, *to = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--layer") && i + 1 < argc) layer = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--key") && i + 1 < argc) keyname = argv[++i];
        else if (!strcmp(argv[i], "--to") && i + 1 < argc) to = argv[++i];
        else { printf("未知参数: %s\n", argv[i]); return 1; }
    }
    Dev d; if (ensure_device(&d)) return 1;
    unsigned char km[KEY_LEN];
    if (read_keymap(&d, (unsigned char)layer, km)) {
        printf("键位读取失败\n"); CloseHandle(d.h); return 1;
    }
    if (list) {
        for (int i = 0; i < NKEYS; i++)
            printf("%-10s idx=%3d  %s\n", KEYS[i].name, KEYS[i].idx,
                   code_text(km + KEYS[i].idx * 4));
        CloseHandle(d.h);
        return 0;
    }
    if (!keyname || !to) {
        printf("请用 --list 查看, 或指定 --key 与 --to\n"); CloseHandle(d.h); return 1;
    }
    int idx = key_index(keyname);
    if (idx < 0) { printf("未知键名: %s\n", keyname); CloseHandle(d.h); return 1; }
    unsigned char code[4];
    if (parse_code(to, code)) { printf("无法解析目标: %s\n", to); CloseHandle(d.h); return 1; }
    printf("%s (idx=%d): %s -> %s\n", keyname, idx,
           code_text(km + idx * 4), code_text(code));
    memcpy(km + idx * 4, code, 4);
    km[127 * 4] = 0; km[127 * 4 + 1] = 0; km[127 * 4 + 2] = 0x5a; km[127 * 4 + 3] = 0xa5;
    if (dev_write(&d, 0, (unsigned char)layer, km, KEY_LEN)) {
        printf("键位写入失败\n"); CloseHandle(d.h); return 1;
    }
    unsigned char chk[KEY_LEN];
    if (read_keymap(&d, (unsigned char)layer, chk) || memcmp(chk + idx * 4, code, 4)) {
        printf("写入后校验失败!\n"); CloseHandle(d.h); return 1;
    }
    printf("已写入并校验通过。\n");
    CloseHandle(d.h);
    return 0;
}

static int cmd_dump(const char *path) {
    Dev d; if (ensure_device(&d)) return 1;
    unsigned char km0[KEY_LEN], km1[KEY_LEN], led[LED_LEN];
    if (read_keymap(&d, 0, km0) || read_keymap(&d, 1, km1) || read_led(&d, led)) {
        printf("读取失败\n"); CloseHandle(d.h); return 1;
    }
    FILE *f = NULL;
    fopen_s(&f, path, "wb");
    if (!f) { printf("无法写入文件: %s\n", path); CloseHandle(d.h); return 1; }
    fwrite(km0, 1, KEY_LEN, f);
    fwrite(km1, 1, KEY_LEN, f);
    fwrite(led, 1, LED_LEN, f);
    fclose(f);
    printf("已备份到 %s (%d 字节)\n", path, KEY_LEN * 2 + LED_LEN);
    CloseHandle(d.h);
    return 0;
}

static int cmd_restore(const char *path) {
    FILE *f = NULL;
    fopen_s(&f, path, "rb");
    if (!f) { printf("无法打开文件: %s\n", path); return 1; }
    unsigned char data[KEY_LEN * 2 + LED_LEN];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (n != sizeof(data)) { printf("备份文件大小不对 (%zu 字节)\n", n); return 1; }
    Dev d; if (ensure_device(&d)) return 1;
    if (dev_write(&d, 0, 0, data, KEY_LEN) ||
        dev_write(&d, 0, 1, data + KEY_LEN, KEY_LEN) ||
        dev_write(&d, 1, 0, data + KEY_LEN * 2, LED_LEN)) {
        printf("恢复写入失败\n"); CloseHandle(d.h); return 1;
    }
    printf("已从 %s 恢复。\n", path);
    CloseHandle(d.h);
    return 0;
}

static void usage(void) {
    printf("AULA F75 配置工具 (原生 C, 零依赖)\n\n");
    printf("用法:\n");
    printf("  f75.exe info                                  查看状态\n");
    printf("  f75.exe light --effect N [--brightness 0-9] [--speed 0-4] [--colour 0-7]\n");
    printf("  f75.exe light --rgb RRGGBB                    逐键自定义色\n");
    printf("  f75.exe key --list                            列出键位\n");
    printf("  f75.exe key --key LWin --to LWin              改键 (disabled/Ctrl+C/raw:..)\n");
    printf("  f75.exe dump --out backup.bin                 备份\n");
    printf("  f75.exe restore backup.bin                    恢复\n\n");
    printf("灯效: 0关闭 1静态 2呼吸 3彩虹 4流光 5雨滴 6彩虹轮盘 7涟漪 8星光\n");
    printf("      9渐隐 10贪吃蛇 11霓虹 12触发 13正弦波 14扫描 15风车\n");
    printf("      16瀑布 17花开 18风暴 21逐键自定义\n");
    printf("颜色: 0红 1绿 2蓝 3黄 4品红 5青 6白 7随机\n");
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) { usage(); return 0; }
    if (!strcmp(argv[1], "info")) return cmd_info();
    if (!strcmp(argv[1], "light")) return cmd_light(argc, argv);
    if (!strcmp(argv[1], "key")) return cmd_key(argc, argv);
    if (!strcmp(argv[1], "dump")) {
        const char *path = "f75-backup.bin";
        for (int i = 2; i < argc; i++)
            if (!strcmp(argv[i], "--out") && i + 1 < argc) path = argv[++i];
        return cmd_dump(path);
    }
    if (!strcmp(argv[1], "restore") && argc > 2) return cmd_restore(argv[2]);
    if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) { usage(); return 0; }
    usage();
    return 1;
}
