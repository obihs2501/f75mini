#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dbt.h>
#include "device.hpp"
#include "model.hpp"
#include "snapshot.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace f75;
using std::wstring;
constexpr wchar_t MainClass[] = L"F75Mini.Workbench";
constexpr wchar_t ViewClass[] = L"F75Mini.Workspace";
constexpr UINT_PTR PollTimer = 1, RefreshTimer = 2;
constexpr UINT FitFocus = WM_APP + 1;
constexpr GUID HidClass{0x4D1E55B2, 0xF16F, 0x11CF, {0x88,0xCB,0x00,0x11,0x11,0x00,0x00,0x30}};
constexpr std::array<const wchar_t*,5> pages{L"设备", L"灯光", L"键位", L"宏", L"配置"};
constexpr std::array<const wchar_t*,8> colors{L"红",L"绿",L"蓝",L"黄",L"品红",L"青",L"白",L"随机"};
constexpr std::array<COLORREF,7> rgb{RGB(225,55,69),RGB(52,169,98),RGB(72,110,230),RGB(238,182,40),RGB(212,76,173),RGB(39,170,180),RGB(210,218,205)};

enum Id : int {
    Brand = 1, Connection, Theme, Zoom, Footer, Undo, Apply, Nav = 20,
    Title = 30, Intro, Candidate, Scan, Read, DemoSwitch, DeviceInfo, Diagnose, GoKeys, GoConfig,
    InputCaption, InputField, DiagnosticText, LayoutCaption, Legend, TaskTitle, SourceInfo,
    EffectLabel, EffectBox, BrightLabel, BrightBox, SpeedLabel, SpeedBox, ColorLabel, ColorBox,
    KeyLabel, KeyTarget, KeyDefault, Help, MacroBody, ConfigHead, ConfigBody, Export, Import,
    DangerHead, DangerBody, KeyFirst = 200
};

enum class Page { Device, Light, Keys, Macro, Config };
enum class Work { None, Scan, Read };

struct Palette { COLORREF bg, paper, ink, quiet, line, accent, danger, hilite, onAccent; };
Palette palette(bool dark) {
    HIGHCONTRASTW hc{sizeof(hc)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON))
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_HOTLIGHT), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    if (dark) return {RGB(23,25,27), RGB(32,35,38), RGB(236,238,235), RGB(171,179,173), RGB(100,112,105), RGB(130,199,162), RGB(255,180,171), RGB(45,64,54), RGB(23,38,30)};
    return {RGB(243,242,238), RGB(252,251,248), RGB(35,40,38), RGB(93,101,95), RGB(142,151,143), RGB(39,99,74), RGB(179,38,30), RGB(223,237,225), RGB(255,255,255)};
}

struct JobResult { DiscoveryResult found; std::optional<DeviceSnapshot> read; };
struct Saved { DeviceCandidate device; DeviceSnapshot data; bool stale{}; };

struct Gui {
    HINSTANCE instance{};
    HWND window{}, view{};
    HDEVNOTIFY notification{};
    std::array<HWND, 300> c{};
    std::array<bool, 300> used{};
    HFONT regular{}, small{}, heading{}, section{};
    HBRUSH pageBrush{}, paperBrush{};
    Palette p{};
    UINT dpi{96};
    int zoom{100};
    int sx{}, sy{}, viewW{}, viewH{}, contentW{}, contentH{};
    RECT keyboardPanel{}, detailPanel{};
    bool dark{}, smoke{}, quitAfterJob{}, scanAgain{}, diagnostic{}, demo{}, layoutBusy{};
    bool foundOnce{};
    Page page{Page::Device};
    Work work{Work::None};
    std::future<JobResult> job;
    std::vector<DeviceCandidate> candidates;
    DeviceIssue scanIssue;
    int candidate{-1};
    DeviceCandidate readTarget;
    std::optional<Saved> snapshot;
    std::optional<DeviceSnapshot> lastRead;
    size_t key{};
    Demo applied, draft;
    wstring demoNote, deviceNote;
    bool demoError{}, deviceError{};

    ~Gui() {
        for (auto obj : {regular, small, heading, section}) if (obj) DeleteObject(obj);
        for (auto obj : {pageBrush, paperBrush}) if (obj) DeleteObject(obj);
    }

    int px(int dip) const { return MulDiv(dip, static_cast<int>(dpi), 96); }
    int dip(int pixel) const { return MulDiv(pixel, 96, static_cast<int>(dpi)); }
    int metric(int value) const { return MulDiv(value, zoom, 100); }
    HWND at(int n) const { return c[static_cast<size_t>(n)]; }
    void text(int n, const wstring& s) const { if (at(n)) SetWindowTextW(at(n), s.c_str()); }
    void enabled(int n, bool value) const { EnableWindow(at(n), value); }
    int selection(int n) const { return static_cast<int>(SendMessageW(at(n), CB_GETCURSEL, 0, 0)); }
    void select(int n, int index) const { SendMessageW(at(n), CB_SETCURSEL, static_cast<WPARAM>(index), 0); }
    void item(int n, const wchar_t* label) const { SendMessageW(at(n), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label)); }
    bool lightDirty() const { return draft.light != applied.light; }
    bool keysDirty() const { return draft.mapping != applied.mapping; }
    int changedKeys() const {
        int count = 0;
        for (size_t i = 0; i < draft.mapping.size(); ++i) if (draft.mapping[i] != applied.mapping[i]) ++count;
        return count;
    }
    const DeviceCandidate* selected() const {
        return candidate >= 0 && static_cast<size_t>(candidate) < candidates.size() ? &candidates[static_cast<size_t>(candidate)] : nullptr;
    }
    bool current() const { return snapshot && !snapshot->stale && selected() && selected()->path == snapshot->device.path; }
    bool canExport() const {
        if (!current() || work != Work::None) return false;
        const auto& s = snapshot->data;
        return s.state == ConnectionState::Ready && (s.lighting || s.system_mode || s.sleep_seconds || s.base_layer);
    }

    void make(int id, const wchar_t* cls, const wchar_t* label, DWORD style, bool global = false, DWORD ex = 0) {
        HWND h = CreateWindowExW(ex, cls, label, WS_CHILD | style, 0,0,0,0,
            global ? window : view, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        c[static_cast<size_t>(id)] = h;
        if (h && !global && (style & WS_TABSTOP)) SetWindowSubclass(h, ChildProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    void label(int n, const wchar_t* s, bool global = false) { make(n, L"STATIC", s, SS_NOPREFIX, global); }
    void button(int n, const wchar_t* s, bool global = false) { make(n, L"BUTTON", s, BS_OWNERDRAW | WS_TABSTOP, global); }
    void combo(int n) { make(n, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP); }

    bool create() {
        view = CreateWindowExW(WS_EX_CONTROLPARENT, ViewClass, L"工作区", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_CLIPCHILDREN,
                               0,0,1,1,window,nullptr,instance,this);
        if (!view) return false;
        label(Brand,L"F75 配置工具",true); label(Connection,L"",true);
        button(Theme,L"深色主题",true); button(Zoom,L"缩放 100%",true);
        for (size_t i=0; i<pages.size(); ++i) button(Nav+static_cast<int>(i),pages[i],true);
        label(Title,L""); label(Intro,L"");
        combo(Candidate); button(Scan,L"扫描设备"); button(Read,L"读取选中设备"); button(DemoSwitch,L"进入演示");
        label(DeviceInfo,L""); button(Diagnose,L"检查 Win 键"); button(GoKeys,L"查看键位"); button(GoConfig,L"查看配置");
        label(InputCaption,L"在此测试 Win 键；只观察本窗口收到的事件：");
        make(InputField,L"EDIT",L"",WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,false,WS_EX_CLIENTEDGE);
        label(DiagnosticText,L"");
        label(LayoutCaption,L"标准 F75 ANSI · 80 键 + 旋钮");
        for (size_t i=0; i<keys().size(); ++i) button(KeyFirst+static_cast<int>(i),wstring(keys()[i].name).c_str());
        label(Legend,L""); label(TaskTitle,L""); label(SourceInfo,L"");
        label(EffectLabel,L"灯效"); combo(EffectBox);
        label(BrightLabel,L"亮度（0–7）"); combo(BrightBox);
        label(SpeedLabel,L"速度（0–7）"); combo(SpeedBox);
        label(ColorLabel,L"预设颜色"); combo(ColorBox);
        label(KeyLabel,L"拟应用映射 · 普通按键或禁用"); combo(KeyTarget); button(KeyDefault,L"恢复此键默认映射");
        label(Help,L""); label(MacroBody,L""); label(ConfigHead,L"本地配置文件"); label(ConfigBody,L"");
        button(Export,L"导出"); button(Import,L"导入演示草稿");
        label(DangerHead,L"危险区域 · 未开放"); label(DangerBody,L"");
        label(Footer,L"",true); button(Undo,L"撤销本页草稿",true); button(Apply,L"应用到演示",true);
        for (int i=Brand;i<=Apply;++i) if (!at(i)) return false;
        for (int i=Nav;i<Nav+static_cast<int>(pages.size());++i) if (!at(i)) return false;
        for (int i=Title;i<=DangerBody;++i) if (!at(i)) return false;
        for (size_t i=0;i<keys().size();++i) if (!at(KeyFirst+static_cast<int>(i))) return false;
        resetList();
        for (const auto& e : effects()) item(EffectBox,wstring(e.name).c_str());
        for (int i=0;i<=7;++i) { const auto n=std::to_wstring(i); item(BrightBox,n.c_str()); item(SpeedBox,n.c_str()); }
        for (auto s : colors) item(ColorBox,s);
        for (const auto& t : targets()) item(KeyTarget,t.c_str());
        dpi = GetDpiForWindow(window);
        appearance();
        update();
        SetFocus(at(Nav));
        if (!smoke) {
            DEV_BROADCAST_DEVICEINTERFACE_W filter{};
            filter.dbcc_size = sizeof(filter); filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE; filter.dbcc_classguid = HidClass;
            notification = RegisterDeviceNotificationW(window,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
            begin(Work::Scan);
        }
        return true;
    }

    void appearance() {
        p = palette(dark);
        const auto font = [this](int size,int weight) { return CreateFontW(-px(metric(size)),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI"); };
        HFONT old[]{regular,small,heading,section};
        regular=font(14,FW_NORMAL); small=font(12,FW_NORMAL); heading=font(25,FW_SEMIBOLD); section=font(17,FW_SEMIBOLD);
        for (size_t i=0;i<c.size();++i) if (c[i]) {
            HFONT h = (i==Title ? heading : (i==Brand || i==TaskTitle || i==ConfigHead || i==DangerHead) ? section : (i>=KeyFirst || i==Legend || i==LayoutCaption) ? small : regular);
            SendMessageW(c[i],WM_SETFONT,reinterpret_cast<WPARAM>(h),TRUE);
        }
        for (auto h:old) if (h) DeleteObject(h);
        if (pageBrush) DeleteObject(pageBrush);
        if (paperBrush) DeleteObject(paperBrush);
        pageBrush=CreateSolidBrush(p.bg); paperBrush=CreateSolidBrush(p.paper);
        if (HMODULE dwm=LoadLibraryW(L"dwmapi.dll")) {
            using Attr = HRESULT (WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
            if (auto set=reinterpret_cast<Attr>(GetProcAddress(dwm,"DwmSetWindowAttribute"))) { const BOOL on=dark; set(window,20,&on,sizeof(on)); }
            FreeLibrary(dwm);
        }
        text(Theme,dark?L"浅色主题":L"深色主题");
        text(Zoom,L"缩放 " + std::to_wstring(zoom) + L"%");
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
    }

    void place(int n, int x, int y, int w, int h, bool global = false, bool dropdown = false) {
        x=px(x); y=px(y); w=px(std::max(1,w)); h=px(std::max(1,h));
        if (!global) { x-=sx; y-=sy; }
        SetWindowPos(at(n),nullptr,x,y,w,dropdown?px(metric(240)):h,SWP_NOZORDER|SWP_NOACTIVATE);
        used[static_cast<size_t>(n)] = true;
    }

    void scrollbars() {
        SCROLLINFO h{sizeof(h),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
        h.nMax=std::max(0,contentW-1); h.nPage=static_cast<UINT>(viewW); h.nPos=sx;
        SCROLLINFO v{sizeof(v),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
        v.nMax=std::max(0,contentH-1); v.nPage=static_cast<UINT>(viewH); v.nPos=sy;
        SetScrollInfo(view,SB_HORZ,&h,TRUE); SetScrollInfo(view,SB_VERT,&v,TRUE);
    }

    void layout() {
        if (!view || layoutBusy) return;
        layoutBusy=true;
        RECT bounds{}; GetClientRect(window,&bounds);
        const int w=dip(bounds.right), h=dip(bounds.bottom);
        const int nav=metric(w<900 ? 82 : 104);
        const int head=metric(w<790 ? 78 : 60), foot=metric(60), btn=metric(38), pad=metric(16);
        // Scrollbars are always reserved so adding a scrollbar never changes the responsive breakpoint.
        SetWindowPos(view,nullptr,px(nav),px(head),std::max(1,px(w-nav)),std::max(1,px(h-head-foot)),SWP_NOZORDER|SWP_NOACTIVATE);
        GetClientRect(view,&bounds); viewW=std::max(1,static_cast<int>(bounds.right)); viewH=std::max(1,static_cast<int>(bounds.bottom));
        const int cw=std::max(dip(viewW),metric(570));
        const int wideAt=metric(925);
        const bool side = cw>=wideAt;
        const int pane = metric(273), gap=metric(14);
        const int kw = cw-2*pad-(side?pane+gap:0), rx = side ? pad+kw+gap : pad, rw=side?pane:kw;
        const int start=metric(98), keyH=metric(34), pitch=metric(38), boardH=metric(284);
        const int ry=side?start:start+boardH+gap;
        const int actionW=metric(134);
        int end=metric(450);
        if (page==Page::Device) end=metric(diagnostic ? 790 : 460);
        if (page==Page::Light || page==Page::Keys) end=std::max(start+boardH,ry+metric(page==Page::Light ? (demo?460:336) : 345))+pad;
        if (page==Page::Macro) end=metric(390);
        if (page==Page::Config) end=metric(510);
        contentW=px(cw); contentH=px(end);
        sx=std::clamp(sx,0,std::max(0,contentW-viewW)); sy=std::clamp(sy,0,std::max(0,contentH-viewH));
        scrollbars();
        used.fill(false);
        const int tw=metric(110), zw=metric(110);
        place(Brand,pad,metric(15),metric(165),metric(30),true);
        place(Theme,w-2*pad-tw-zw,metric(10),tw,btn,true);
        place(Zoom,w-pad-zw,metric(10),zw,btn,true);
        if (head==metric(78)) place(Connection,pad,metric(47),w-2*pad,metric(26),true);
        else place(Connection,metric(182),metric(19),w-metric(182)-tw-zw-3*pad,metric(29),true);
        for (int i=0;i<static_cast<int>(pages.size());++i) place(Nav+i,metric(8),head+metric(14)+i*metric(47),nav-metric(16),btn,true);
        const bool actions=demo && (page==Page::Light || page==Page::Keys);
        place(Footer,pad,h-foot+metric(8),w-2*pad-(actions?2*actionW+gap:0),foot-metric(12),true);
        if (actions) {
            place(Undo,w-2*actionW-gap-pad,h-foot+metric(9),actionW,btn,true);
            place(Apply,w-actionW-pad,h-foot+metric(9),actionW,btn,true);
        }
        place(Title,pad,metric(16),cw-2*pad,metric(35));
        place(Intro,pad,metric(57),cw-2*pad,metric(39));
        keyboardPanel={}; detailPanel={};
        if (page==Page::Device) {
            place(Candidate,pad,start,cw-2*pad-actionW*2-2*gap,btn,false,true);
            place(Scan,cw-pad-2*actionW-gap,start,actionW,btn);
            place(Read,cw-pad-actionW,start,actionW,btn);
            place(DeviceInfo,pad,metric(149),cw-2*pad,metric(245));
            place(DemoSwitch,pad,metric(404),actionW,btn);
            place(Diagnose,pad+actionW+gap,metric(404),actionW,btn);
            place(GoKeys,pad+2*(actionW+gap),metric(404),metric(96),btn);
            place(GoConfig,pad+2*(actionW+gap)+metric(96)+gap,metric(404),metric(96),btn);
            if (diagnostic) {
                place(InputCaption,pad,metric(469),cw-2*pad,metric(26));
                place(InputField,pad,metric(499),std::min(metric(440),cw-2*pad),btn);
                place(DiagnosticText,pad,metric(555),cw-2*pad,metric(209));
            }
        } else if (page==Page::Light || page==Page::Keys) {
            keyboardPanel={px(pad)-sx,px(start)-sy,px(pad+kw)-sx,px(start+boardH)-sy};
            detailPanel={px(rx)-sx,px(ry)-sy,px(rx+rw)-sx,px(end-pad/2)-sy};
            place(LayoutCaption,pad+gap,start+metric(10),kw-2*gap,metric(22));
            int max=0;
            for (const auto& k:keys()) max=std::max(max,k.x+k.w);
            const int span=std::min(kw-2*gap,metric(740));
            const int left=pad+(kw-span)/2;
            for (size_t i=0;i<keys().size();++i) {
                const auto& k=keys()[i];
                int a=MulDiv(k.x,span,max), b=MulDiv(k.x+k.w,span,max);
                place(KeyFirst+static_cast<int>(i),left+a,start+metric(35)+k.y*pitch,b-a-metric(3),keyH);
            }
            place(Legend,pad+gap,start+metric(260),kw-2*gap,metric(21));
            const int ax=rx+gap, aw=rw-2*gap;
            place(TaskTitle,ax,ry+metric(12),aw,metric(31));
            place(SourceInfo,ax,ry+metric(49),aw,metric(demo?95:180));
            if (demo && page==Page::Light) {
                const auto& e=effects()[static_cast<size_t>(draft.light.effect)];
                place(EffectLabel,ax,ry+metric(148),aw,metric(22));
                place(EffectBox,ax,ry+metric(170),aw,btn,false,true);
                int y=ry+metric(218);
                if (draft.light.effect!=0) {
                    place(BrightLabel,ax,y,aw,metric(22)); place(BrightBox,ax,y+metric(23),aw,btn,false,true); y+=metric(64);
                }
                if (e.speed) { place(SpeedLabel,ax,y,aw,metric(22)); place(SpeedBox,ax,y+metric(23),aw,btn,false,true); y+=metric(64); }
                if (e.color) { place(ColorLabel,ax,y,aw,metric(22)); place(ColorBox,ax,y+metric(23),aw,btn,false,true); }
            } else if (demo && page==Page::Keys && keys()[key].editable) {
                place(KeyLabel,ax,ry+metric(149),aw,metric(25));
                place(KeyTarget,ax,ry+metric(176),aw,btn,false,true);
                place(KeyDefault,ax,ry+metric(226),aw,btn);
                place(Help,ax,ry+metric(275),aw,metric(65));
            } else {
                place(Help,ax,ry+metric(demo?153:229),aw,metric(95));
            }
        } else if (page==Page::Macro) {
            place(MacroBody,pad,start,cw-2*pad,metric(240));
        } else {
            place(ConfigHead,pad,start,cw-2*pad,metric(33));
            place(ConfigBody,pad,metric(141),cw-2*pad,metric(151));
            place(Export,pad,metric(294),metric(196),btn);
            if (demo) place(Import,pad+metric(196)+gap,metric(294),metric(158),btn);
            place(DangerHead,pad,metric(391),cw-2*pad,metric(29));
            place(DangerBody,pad,metric(430),cw-2*pad,metric(68));
        }
        for (size_t i=0;i<c.size();++i) if (c[i]) {
            const bool visible=(GetWindowLongPtrW(c[i],GWL_STYLE)&WS_VISIBLE)!=0;
            if (visible!=used[i]) ShowWindow(c[i],used[i]?SW_SHOWNA:SW_HIDE);
        }
        InvalidateRect(view,nullptr,TRUE);
        InvalidateRect(window,nullptr,TRUE);
        layoutBusy=false;
    }

    static wstring issue(const DeviceIssue& value, const wchar_t* fallback=L"无法读取") {
        return value.message.empty()?wstring(fallback):value.message;
    }
    static wstring rawNumber(const std::optional<std::uint8_t>& n) {
        return n ? std::to_wstring(*n) : L"无法读取";
    }
    static wstring mode(const DeviceSnapshot* s) {
        return s && s->system_mode ? wstring(mode_name(*s->system_mode)) : L"无法读取";
    }
    static wstring light(const DeviceSnapshot* s) {
        if (!s) return L"尚未读取";
        if (!s->lighting) return issue(s->lighting_issue);
        const auto& l=*s->lighting;
        wstring line=L"设备灯效编号 " + std::to_wstring(l.effect) + L"，亮度 " + rawNumber(l.brightness) + L"，速度 " + rawNumber(l.speed);
        if (l.colour) line += L"，颜色 " + (*l.colour<colors.size() ? wstring(colors[*l.colour]) : L"编号 " + std::to_wstring(*l.colour));
        return line;
    }
    const DeviceSnapshot* displayed() const {
        if (snapshot && (!selected() || selected()->path==snapshot->device.path)) return &snapshot->data;
        return lastRead ? &*lastRead : nullptr;
    }
    bool showingOld() const { return displayed() && snapshot && displayed()==&snapshot->data && snapshot->stale; }
    wstring status() const {
        if (demo) return L"演示数据，未连接硬件";
        if (work==Work::Scan) return L"正在后台扫描设备…";
        if (work==Work::Read) return L"正在后台读取选中设备…";
        if (lastRead) return wstring(connection_name(lastRead->state));
        if (showingOld()) return L"已断开或尚未确认，显示上次读取结果";
        if (current()) return wstring(transport_name(snapshot->device.transport)) + L" · " + wstring(connection_name(snapshot->data.state));
        if (selected()) return selected()->transport==Transport::Receiver ? L"接收器已发现，键盘尚未确认响应" : L"已发现候选设备，等待读取";
        if (!candidates.empty()) return L"请选择要读取的设备";
        return foundOnce && scanIssue.code!=DeviceError::None ? L"设备扫描未完成" : L"未连接";
    }
    void update() {
        const auto* snap=displayed();
        const auto* dev=selected();
        text(Connection,status());
        text(Title,pages[static_cast<size_t>(page)]);
        enabled(Scan,work==Work::None); enabled(Read,work==Work::None && dev && !demo);
        enabled(Candidate,work==Work::None && !demo && !candidates.empty());
        text(DemoSwitch,demo?L"退出演示":L"进入演示"); enabled(DemoSwitch,work==Work::None);
        switch (page) {
        case Page::Device: {
            text(Intro,demo?L"此会话只使用示例灯光与键位；切回设备模式后才能读取真机。":L"连接 USB-C 数据线或 2.4G 接收器，选择候选设备后读取。蓝牙配置暂未开放。");
            wstring info;
            if (demo) {
                info=L"演示 · 标准 F75 ANSI\n\n本会话有独立的灯光和键位草稿。\n在灯光或键位页应用，只更新演示的当前值；配置页可导入、导出草稿。\n模式、Win 固件锁、电量和休眠不模拟为设备读数。";
            } else if (!dev && !snap) {
                info = candidates.empty() ? L"没有选中的设备。\n\n使用有线模式测试时，请确认使用支持数据传输的 USB-C 线。\n无线模式请插入 2.4G 接收器、切换连接方式并唤醒键盘。" : L"发现多个候选接口。请从列表中明确选择一个，再点击“读取选中设备”。\n候选型号不等于已确认变体，也不作为写入依据。";
                if (scanIssue.code!=DeviceError::None) info+=L"\n\n扫描提示："+issue(scanIssue,L"扫描未完成，请重试。");
            } else {
                const auto* identity=dev ? dev : (snapshot ? &snapshot->device : nullptr);
                if (identity) {
                    wchar_t ids[64]{};
                    swprintf_s(ids, L"%04X:%04X  ·  版本 %04X", static_cast<unsigned>(identity->vid),static_cast<unsigned>(identity->pid),static_cast<unsigned>(identity->version));
                    info=(identity->name.empty()?L"F75 候选设备":identity->name)+L"  ·  "+wstring(transport_name(identity->transport))+L"\n识别详情："+ids;
                    if (identity->issue.code!=DeviceError::None && !snap) info+=L"\n接口提示："+issue(identity->issue);
                }
                info+=L"\n\n"+status()+L"\n";
                if (snap && snap->state==ConnectionState::Ready) {
                    info+=L"系统模式："+mode(snap)+L"  ·  Win 固件锁：无法读取\n灯光："+light(snap);
                    info+=L"\n基础键层："+wstring(snap->base_layer?L"已读取原始矩阵（未核对物理键对应关系）":L"无法读取");
                    info+=L"\n休眠："+(snap->sleep_seconds ? (*snap->sleep_seconds ? std::to_wstring(*snap->sleep_seconds)+L" 秒" : L"关闭") : L"无法读取");
                    info+=L"  ·  电量：";
                    if (snap->battery && snap->battery->power==BatteryPower::ExternalPower) info+=L"外接供电，不显示百分比";
                    else if (snap->battery && snap->battery->percent) info+=std::to_wstring(*snap->battery->percent)+L"%";
                    else info+=dev && dev->transport==Transport::Wired ? L"当前通路无法读取" : L"无法读取（可按 Fn+B 查看键盘指示）";
                    if (showingOld()) info+=L"\n上述数值不是当前设备状态。";
                } else if (snap) {
                    if (snap->issue.code!=DeviceError::None) info+=issue(snap->issue)+L"\n";
                    if (snap->state==ConnectionState::ReceiverUnconfirmed) info+=L"确认供电及 2.4G 模式，将键盘靠近接收器并按键唤醒后重试。";
                    if (snap->state==ConnectionState::Busy) info+=L"关闭可能占用设备的配置程序，然后重试；占用来源未确认。";
                    if (snap->state==ConnectionState::Disconnected) info+=L"检查连接，并重新扫描和读取。";
                    if (snapshot && snapshot->stale && lastRead && selected() && selected()->path==snapshot->device.path)
                        info+=L"\n保留了上一次成功读取，当前结果不可用。";
                } else {
                    info+=L"尚未主动读取该候选接口。接收器出现在列表中，不代表键盘已在线。";
                }
            }
            text(DeviceInfo,info);
            text(Diagnose,diagnostic?L"收起 Win 键排查":L"检查 Win 键");
            text(DiagnosticText,L"1. 检查连接与输入：先确认普通键可用，再用上方焦点框试按 Win。系统可能拦截 Win 键，未收到事件不能证明键盘故障。\n\n2. 检查系统模式："+(demo?wstring(L"演示不读取设备模式"):current()?mode(snap):L"无法确认当前模式")+L"。标准三模 F75 手册：Fn+Q（Android）、Fn+W（Windows）、Fn+E（Mac）。\n\n3. 检查固件锁：软件无法读取。Windows 模式下按 Fn+Win 切换 Win Lock，再测试；该组合不是单向解锁。\n\n4. 检查映射：本版不猜测原始键层的 Win 索引。恢复映射也不能替代解除固件锁。");
            break;
        }
        case Page::Light: {
            text(Intro,demo?L"调整本地演示。灯条仅表示预设颜色和亮度，不模拟动画时序。":L"显示读到的内置灯效。逐键配色、实时灯效、真实写入暂未开放。");
            text(TaskTitle,demo?L"内置灯效 · 演示":L"内置灯效 · 只读");
            text(Legend,demo?L"色条：草稿效果示意 · 可点按键帽查看物理键":L"硬件颜色不画到每个键帽；这里只显示标准布局参考。");
            if (demo) {
                const auto describe=[](const Light& l) {
                    const auto& e=effects()[static_cast<size_t>(l.effect)];
                    wstring d(e.name);
                    if (l.effect!=0) d+=L" / 亮度 "+std::to_wstring(l.brightness);
                    if (e.speed) d+=L" / 速度 "+std::to_wstring(l.speed);
                    if (e.color) d+=L" / "+wstring(colors[static_cast<size_t>(l.color)]);
                    return d;
                };
                text(SourceInfo,L"当前："+describe(applied.light)+L"\n预览："+describe(draft.light));
                select(EffectBox,draft.light.effect); select(BrightBox,draft.light.brightness); select(SpeedBox,draft.light.speed); select(ColorBox,draft.light.color);
            } else {
                text(SourceInfo,(showingOld()?L"上次读取 · 已过期\n":L"设备读取结果\n")+light(snap));
                text(Help,L"编号来自设备协议，不对应演示灯效列表。真实写入未获真机验证。");
            }
            break;
        }
        case Page::Keys: {
            text(Intro,demo?L"选择物理键，在基础层编辑单键映射。用方向键移动，Enter 或空格选中。":L"标准 ANSI 布局仅供参考。原始矩阵与物理键尚未建立可靠对应，不能编辑真机。");
            text(TaskTitle,demo?L"基础层 · 单键映射":L"基础层 · 只读摘要");
            text(Legend,demo?L"描边：已选 · *：有修改 · 虚线：键盘焦点":L"描边：已选物理键 · 虚线：键盘焦点");
            const auto& k=keys()[key];
            wstring data=L"物理键："+wstring(k.name)+L"\n";
            if (demo) {
                data+=L"当前映射："+applied.mapping[key]+L"\n拟应用："+draft.mapping[key];
                const auto i=std::find(targets().begin(),targets().end(),draft.mapping[key]);
                select(KeyTarget,i==targets().end()?-1:static_cast<int>(i-targets().begin()));
                enabled(KeyDefault,k.editable && k.name!=draft.mapping[key]);
                if (k.name==L"Knob") text(Help,L"标准 F75 手册中旋钮用于音量或灯光；本演示不重映射旋转或按压。");
                else if (!k.editable) text(Help,L"Fn 是保留键，保留物理位置，不允许重映射。");
                else if (k.name==L"LWin" || k.name==L"Esc") text(Help,L"更改关键键可能改变操作习惯。在演示中可用“恢复此键默认映射”恢复。映射与固件 Win Lock 无关。");
                else text(Help,L"改动只在草稿中；应用后仅改变演示的当前值。");
            } else {
                data+=snap && snap->base_layer ? L"已读原始矩阵，无法确认此键的映射。" : L"设备当前映射：无法读取";
                if (showingOld()) data+=L"\n数据来自上次读取，已过期。";
                text(Help,k.name==L"Knob" ? L"旋钮用于音量或灯光；尚未验证软件重映射。" : !k.editable ? L"Fn 入口不开放编辑；Fn 层也未开放。" : L"真实写入未经验证。系统模式可在设备页查看；不自动切换模式。");
            }
            text(SourceInfo,data);
            break;
        }
        case Page::Macro:
            text(Intro,L"宏管理尚未开放。");
            text(MacroBody,L"本版不读取、录制、执行或写入宏，也不会在导入演示配置后执行内容。\n\n有线宏已有社区实现，但本项目尚未确认适用设备、通路、步骤容量和播放方式。无线宏也未验证；这不代表硬件本身不支持。\n\n演示导入与设备报告均不包含宏，不提供模拟容量或绑定结果。");
            break;
        case Page::Config: {
            text(Intro,demo?L"演示文件和设备报告是分离的两种格式，不能相互导入。":L"导出当前通路已读取的内容；这是报告，不是整板备份。真实恢复未开放。");
            text(ConfigBody,demo?L"导出灯光与基础层键位草稿（包含未应用修改）。导入前验证版本、ANSI 布局和取值，并预览覆盖范围。\n\n文件只用于演示；不含宏、电源、逐键配色、Win 固件锁或板载档位。":L"可包含：灯光、系统模式、休眠及基础键层的已读取项；具体范围见报告。\n\n不包含宏、逐键配色、Win 固件锁或板载档位。文件不支持恢复到真机。电量是状态，不是可恢复配置。\n\n无当前读取结果、读取中或快照已过期时，不能导出。");
            text(Export,demo?L"导出演示草稿":L"导出可读取配置"); enabled(Export,demo||canExport());
            text(DangerBody,L"软件不发送恢复出厂或清空档位命令。仅在确认手册适用且接受整板配置丢失时，参考 EPOMAKER 标准三模 F75 手册（Fn+Esc 长按 3 秒）。这不是 Win 键排查的首选。 ");
            break;
        }
        }
        if (page==Page::Light || page==Page::Keys) {
            for (size_t i=0;i<keys().size();++i) {
                wstring name(keys()[i].name);
                if (page==Page::Keys && demo) {
                    name+=L"，映射为 "+draft.mapping[i];
                    if (draft.mapping[i]!=applied.mapping[i]) name+=L"，有未应用修改";
                }
                if (i==key) name+=L"，已选中";
                text(KeyFirst+static_cast<int>(i),name);
            }
        }
        const bool pageDirty=page==Page::Light?lightDirty():page==Page::Keys && keysDirty();
        enabled(Undo,demo&&pageDirty); enabled(Apply,demo&&pageDirty);
        wstring foot;
        if (demo) {
            if (demoError) foot=demoNote;
            else if (lightDirty()||keysDirty()) {
                foot=L"未应用：";
                if (lightDirty()) foot+=L"灯光";
                if (lightDirty()&&keysDirty()) foot+=L"、";
                if (keysDirty()) foot+=std::to_wstring(changedKeys())+L" 个键";
                if (page==Page::Light||page==Page::Keys) foot+=L" · 仅应用当前页";
                if (!demoNote.empty()) foot+=L"\n"+demoNote;
            }
            else foot=demoNote.empty()?L"没有未应用修改 · 不会写入键盘":demoNote;
        } else if (work!=Work::None) foot=status();
        else foot=deviceNote.empty()?L"只读模式 · 未经真机验证，不开放真实写入":deviceNote;
        text(Footer,foot);
        layout();
    }

    void resetList() {
        SendMessageW(at(Candidate),CB_RESETCONTENT,0,0);
        if (demo) { item(Candidate,L"独立演示会话 · 不读取硬件"); select(Candidate,0); return; }
        for (size_t i=0;i<candidates.size();++i) {
            const auto& d=candidates[i];
            const auto line=std::to_wstring(i+1)+L". "+(d.name.empty()?L"F75 候选接口":d.name)+L" · "+wstring(transport_name(d.transport));
            item(Candidate,line.c_str());
        }
        if (candidates.empty()) item(Candidate,L"无候选设备");
        select(Candidate,candidates.empty()?0:candidate);
    }

    void begin(Work next) {
        if (work!=Work::None || (next==Work::Read && (demo||!selected()))) return;
        const DeviceCandidate target=next==Work::Read?*selected():DeviceCandidate{};
        if (next==Work::Read) readTarget=target;
        try {
            // No HWND or GUI state is captured. The window thread alone consumes results.
            job=std::async(std::launch::async,[next,target] {
                JobResult result;
                if (next==Work::Scan) result.found=discover_devices();
                else result.read=read_device(target);
                return result;
            });
        } catch (...) {
            deviceNote=L"无法启动后台任务，请重试。"; deviceError=true;
            update(); return;
        }
        work=next;
        if (!SetTimer(window,PollTimer,60,nullptr)) {
            // WM_APP wakeups are not used; installation failure is fatal to this UI session.
            // The future is still owned until shutdown so no worker uses destroyed state.
            MessageBoxW(window,L"无法启动后台结果计时器。请重开工具。",L"后台任务",MB_ICONERROR);
            DestroyWindow(window);
            return;
        }
        update();
    }

    void acceptScan(DiscoveryResult result) {
        const auto oldPath=selected()?selected()->path:wstring{};
        auto same=[&oldPath](const DeviceCandidate& d){ return d.path==oldPath; };
        foundOnce=true; scanIssue=std::move(result.issue);
        const bool scanFailed=scanIssue.code!=DeviceError::None;
        candidates=std::move(result.candidates);
        const auto i=std::find_if(candidates.begin(),candidates.end(),same);
        const int newIndex=i==candidates.end() ? (candidates.size()==1?0:-1) : static_cast<int>(i-candidates.begin());
        if (i==candidates.end()) {
            lastRead.reset();
            if (snapshot) snapshot->stale=true;
        }
        if (scanFailed && snapshot) snapshot->stale=true;
        candidate=newIndex;
        resetList();
        deviceError=scanFailed;
        deviceNote=scanFailed?issue(scanIssue,L"扫描未完成，请重试。") : L"扫描完成；请选择接口后手动读取。";
    }

    void poll() {
        if (work==Work::None || job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        KillTimer(window,PollTimer);
        const auto completed=std::exchange(work,Work::None);
        try {
            auto value=job.get();
            if (completed==Work::Scan) acceptScan(std::move(value.found));
            else if (value.read && selected() && selected()->path==readTarget.path) {
                // A topology change while reading makes an otherwise good result stale.
                if (value.read->state==ConnectionState::Ready) {
                    snapshot=Saved{readTarget,std::move(*value.read),scanAgain};
                    lastRead.reset();
                    deviceError=false;
                    deviceNote=scanAgain?L"设备连接发生变化；请重新读取。":L"读取完成 · 只读，未写入键盘";
                } else {
                    if (snapshot && snapshot->device.path==readTarget.path) snapshot->stale=true;
                    lastRead=std::move(*value.read);
                    deviceError=true;
                    deviceNote=issue(lastRead->issue,L"未获得可用读取结果，请检查连接后重试。");
                }
            }
        } catch (...) {
            if (snapshot) snapshot->stale=true;
            deviceError=true;
            deviceNote=completed==Work::Scan?L"后台扫描失败，请重试。":L"后台读取失败，请重新读取。";
        }
        if (quitAfterJob) { DestroyWindow(window); return; }
        if (std::exchange(scanAgain,false)) begin(Work::Scan);
        else update();
    }

    void hotplug(bool invalidate=true) {
        if (smoke || quitAfterJob) return;
        if (snapshot && invalidate) snapshot->stale=true;
        if (invalidate) lastRead.reset();
        if (work!=Work::None) scanAgain=true;
        else SetTimer(window,RefreshTimer,450,nullptr);
        if (invalidate) update();
    }

    std::optional<std::filesystem::path> filePath(bool save, bool sample) {
        std::array<wchar_t,32768> name{};
        constexpr wchar_t demoFilter[]=L"F75 演示文件 (*.f75demo)\0*.f75demo\0\0";
        constexpr wchar_t snapshotFilter[]=L"JSON 设备报告 (*.json)\0*.json\0\0";
        if (save) wcscpy_s(name.data(),name.size(),sample?L"F75-演示草稿.f75demo":L"F75-只读报告.json");
        OPENFILENAMEW of{sizeof(of)};
        of.hwndOwner=window; of.lpstrFile=name.data(); of.nMaxFile=static_cast<DWORD>(name.size());
        of.lpstrFilter=sample?demoFilter:snapshotFilter; of.lpstrDefExt=sample?L"f75demo":L"json";
        of.lpstrTitle=save?(sample?L"导出演示草稿":L"导出可读取配置"):L"导入演示草稿";
        of.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if ((save?GetSaveFileNameW(&of):GetOpenFileNameW(&of))) {
            if (name[0]==L'\\' && name[1]==L'\\') { report(L"离线模式只允许本地文件路径。",true); return std::nullopt; }
            const wchar_t root[]={name[0],L':',L'\\',L'\0'};
            if (name[1]==L':' && GetDriveTypeW(root)==DRIVE_REMOTE) { report(L"离线模式不读写网络映射驱动器。",true); return std::nullopt; }
            return std::filesystem::path(name.data());
        }
        if (CommDlgExtendedError()) report(L"无法打开文件选择器。",true);
        return std::nullopt;
    }

    void report(wstring message,bool failed=false) {
        if (demo) { demoNote=std::move(message); demoError=failed; }
        else { deviceNote=std::move(message); deviceError=failed; }
        update();
    }
    bool exportFile() {
        const bool sample=demo;
        if (!sample && !canExport()) return false;
        const auto devicePath=selected()?selected()->path:wstring{};
        const auto dest=filePath(true,sample);
        if (!dest) return false;
        if (!sample && (!canExport() || !selected() || selected()->path!=devicePath)) {
            report(L"当前设备或快照已变化，没有导出。请重新读取。",true); return false;
        }
        wstring error;
        try {
            const bool ok=sample?save_demo(*dest,draft,error):save_snapshot(*dest,snapshot->device,snapshot->data,error);
            if (ok) { report(sample?L"已导出演示草稿，未写入键盘。":L"已导出可读取配置；不是整板备份。"); return true; }
        } catch (...) { error=L"无法生成输出文件。"; }
        report(error.empty()?L"导出失败，请检查路径后重试。":error,true);
        return false;
    }
    void importFile() {
        if (!demo) return;
        const auto path=filePath(false,true);
        if (!path) return;
        wstring error;
        std::optional<Demo> incoming;
        try { incoming=load_demo(*path,error); }
        catch (...) { error=L"无法读取演示文件。"; }
        if (!incoming) { report(error.empty()?L"导入失败；演示草稿未改变。":error,true); return; }
        int edits=0;
        wstring preview;
        for (size_t i=0;i<keys().size();++i) if (draft.mapping[i]!=incoming->mapping[i]) {
            ++edits;
            if (edits<=6) preview+=wstring(keys()[i].name)+L"："+draft.mapping[i]+L" → "+incoming->mapping[i]+L"\n";
        }
        if (*incoming==draft) { report(L"文件与当前演示草稿一致。未执行导入。"); return; }
        const auto& l=incoming->light;
        const auto& e=effects()[static_cast<size_t>(l.effect)];
        wstring confirm=L"将替换演示草稿中的灯光和基础层键位，不改变演示当前值，也不写入键盘。\n\n";
        confirm+=L"灯光："+wstring(incoming->light==draft.light?L"无差异":L"有差异")+L"；新草稿为 "+wstring(e.name);
        if (l.effect!=0) confirm+=L" / 亮度 "+std::to_wstring(l.brightness);
        if (e.speed) confirm+=L" / 速度 "+std::to_wstring(l.speed);
        if (e.color) confirm+=L" / "+wstring(colors[static_cast<size_t>(l.color)]);
        confirm+=L"\n基础层："+std::to_wstring(edits)+L" 个键有差异\n"+preview;
        if (edits>6) confirm+=L"…\n";
        confirm+=L"\n不含宏、电源、逐键配色、Win 固件锁或板载档位。\n要将该文件导入为新草稿吗？";
        if (MessageBoxW(window,confirm.c_str(),L"核对导入范围",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES) {
            draft=std::move(*incoming);
            report(L"已导入独立演示草稿；请分别在灯光、键位页审阅和应用。");
        }
    }
    bool settleDemo() {
        if (!demo || (!lightDirty() && !keysDirty())) return true;
        const int answer=MessageBoxW(window,L"演示仍有未应用修改。\n\n是：导出草稿后继续\n否：放弃未应用修改后继续\n取消：留在当前会话",L"处理演示草稿",MB_YESNOCANCEL|MB_DEFBUTTON3|MB_ICONWARNING);
        if (answer==IDYES && !exportFile()) return false;
        if (answer!=IDYES && answer!=IDNO) return false;
        draft=applied;
        return true;
    }
    void close() {
        if (!settleDemo()) return;
        if (work!=Work::None) {
            quitAfterJob=true; KillTimer(window,RefreshTimer);
            text(Connection,L"正在结束后台读取或扫描…");
            ShowWindow(window,SW_HIDE);
        } else DestroyWindow(window);
    }
    void go(Page next) {
        page=next; sx=sy=0; update();
        SetFocus(at(Nav+static_cast<int>(page)));
    }
    void command(int id,int code) {
        if (code==CBN_SELCHANGE) {
            const int value=selection(id);
            if (id==Candidate && !demo && work==Work::None) {
                if (candidate!=value) {
                    candidate=value; lastRead.reset();
                    if (snapshot) snapshot->stale=true;
                    deviceNote=L"已切换候选接口，请重新读取。"; deviceError=false; update();
                }
            } else if (demo && value>=0) {
                if (id==EffectBox) draft.light.effect=value;
                else if (id==BrightBox) draft.light.brightness=value;
                else if (id==SpeedBox) draft.light.speed=value;
                else if (id==ColorBox) draft.light.color=value;
                else if (id==KeyTarget && keys()[key].editable) draft.mapping[key]=targets()[static_cast<size_t>(value)];
                else return;
                demoNote.clear(); demoError=false; update();
            }
            return;
        }
        if (code!=BN_CLICKED) return;
        if (id>=KeyFirst && static_cast<size_t>(id-KeyFirst)<keys().size()) {
            key=static_cast<size_t>(id-KeyFirst); update(); return;
        }
        if (id>=Nav && id<Nav+static_cast<int>(pages.size())) { go(static_cast<Page>(id-Nav)); return; }
        switch (id) {
        case Theme: dark=!dark; appearance(); update(); break;
        case Zoom: zoom=zoom==100?115:zoom==115?130:100; appearance(); update(); reveal(GetFocus()); break;
        case Scan: begin(Work::Scan); break;
        case Read: begin(Work::Read); break;
        case DemoSwitch:
            if (work==Work::None && settleDemo()) {
                demo=!demo; demoNote.clear(); demoError=false; resetList(); update();
            }
            break;
        case Diagnose:
            diagnostic=!diagnostic;
            text(InputField,L"先试普通键，再试 Win 键"); update();
            if (diagnostic) SetFocus(at(InputField));
            break;
        case GoKeys: go(Page::Keys); break;
        case GoConfig: go(Page::Config); break;
        case KeyDefault: if (demo && keys()[key].editable) { draft.mapping[key]=keys()[key].name; report(L"已在草稿中恢复此键默认映射。"); } break;
        case Undo:
            if (!demo) break;
            if (page==Page::Light) draft.light=applied.light;
            if (page==Page::Keys) draft.mapping=applied.mapping;
            report(L"已撤销本页未应用的编辑。其它页面不变。"); break;
        case Apply:
            if (!demo) break;
            if (page==Page::Light) applied.light=draft.light;
            if (page==Page::Keys) applied.mapping=draft.mapping;
            report(L"已应用到演示；没有写入键盘。"); break;
        case Export: exportFile(); break;
        case Import: importFile(); break;
        default: break;
        }
    }

    void scroll(int bar, int code) {
        const bool vertical=bar==SB_VERT;
        int& pos=vertical?sy:sx;
        const int pageSize=vertical?viewH:viewW;
        const int old=pos;
        SCROLLINFO s{sizeof(s),SIF_TRACKPOS};
        GetScrollInfo(view,bar,&s);
        switch (code) {
        case SB_TOP: pos=0; break;
        case SB_BOTTOM: pos=vertical?contentH:contentW; break;
        case SB_LINEUP: pos-=px(metric(38)); break;
        case SB_LINEDOWN: pos+=px(metric(38)); break;
        case SB_PAGEUP: pos-=pageSize; break;
        case SB_PAGEDOWN: pos+=pageSize; break;
        case SB_THUMBPOSITION: case SB_THUMBTRACK: pos=s.nTrackPos; break;
        default: break;
        }
        if (old!=pos) layout();
    }
    void reveal(HWND h) {
        if (!h || GetParent(h)!=view || !IsWindowVisible(h)) return;
        RECT r{}; GetWindowRect(h,&r);
        MapWindowPoints(nullptr,view,reinterpret_cast<POINT*>(&r),2);
        int x=sx,y=sy;
        if (r.left<0) x+=r.left; else if (r.right>viewW) x+=r.right-viewW;
        if (r.top<0) y+=r.top; else if (r.bottom>viewH) y+=r.bottom-viewH;
        if (x!=sx || y!=sy) { sx=x; sy=y; layout(); }
    }
    bool arrow(int index, WPARAM dir) {
        if (index<0 || static_cast<size_t>(index)>=keys().size()) return false;
        const auto& here=keys()[static_cast<size_t>(index)];
        const int x=here.x*2+here.w;
        int best=-1,score=1000000;
        for (size_t i=0;i<keys().size();++i) {
            const auto& k=keys()[i];
            const int dx=k.x*2+k.w-x,dy=k.y-here.y;
            const bool vertical=dir==VK_UP||dir==VK_DOWN;
            if (vertical ? (dir==VK_UP ? dy>=0 : dy<=0) : (dy!=0 || (dir==VK_LEFT ? dx>=0 : dx<=0))) continue;
            const int distance=vertical ? std::abs(dy)*250+std::abs(dx) : std::abs(dx);
            if (distance<score) { score=distance; best=static_cast<int>(i); }
        }
        if (best>=0) SetFocus(at(KeyFirst+best));
        return true;
    }

    static LRESULT CALLBACK ChildProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR state) {
        auto* g=reinterpret_cast<Gui*>(state);
        const int control=GetDlgCtrlID(h);
        if (msg==WM_NCDESTROY) RemoveWindowSubclass(h,ChildProc,id);
        if (msg==WM_SETFOCUS) PostMessageW(g->window,FitFocus,reinterpret_cast<WPARAM>(h),0);
        if (msg==WM_GETDLGCODE && control>=KeyFirst) return DefSubclassProc(h,msg,wp,lp)|DLGC_WANTARROWS;
        if (msg==WM_KEYDOWN) {
            if (control>=KeyFirst && wp>=VK_LEFT && wp<=VK_DOWN && g->arrow(control-KeyFirst,wp)) return 0;
            if (control>=KeyFirst && wp==VK_RETURN) { g->command(control,BN_CLICKED); return 0; }
            if (control==InputField && wp!=VK_ESCAPE && wp!=VK_TAB) {
                SetWindowTextW(h,wp==VK_LWIN?L"已收到：左 Win":wp==VK_RWIN?L"已收到：右 Win":L"已收到普通按键事件（不记录输入）");
                return 0;
            }
        }
        return DefSubclassProc(h,msg,wp,lp);
    }

    static void shape(HDC dc, const RECT& r, COLORREF fill, COLORREF line, int radius, int width=1) {
        const HBRUSH b=CreateSolidBrush(fill);
        const HPEN pen=CreatePen(PS_SOLID,width,line);
        const auto oldBrush=SelectObject(dc,b), oldPen=SelectObject(dc,pen);
        RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
        SelectObject(dc,oldBrush); SelectObject(dc,oldPen);
        DeleteObject(b); DeleteObject(pen);
    }
    void draw(const DRAWITEMSTRUCT& item) {
        const int id=static_cast<int>(item.CtlID);
        const bool cap=id>=KeyFirst;
        const bool active=(cap && static_cast<size_t>(id-KeyFirst)==key) || id==Nav+static_cast<int>(page);
        const bool disabled=(item.itemState&ODS_DISABLED)!=0;
        const bool press=(item.itemState&ODS_SELECTED)!=0;
        const bool primary=id==Apply && !disabled;
        const bool accentText=active && !cap;
        COLORREF bg=primary?p.accent:active||press?p.hilite:p.paper;
        const COLORREF edge=primary||active?p.accent:p.line;
        RECT r=item.rcItem;
        FillRect(item.hDC,&r,cap?paperBrush:(GetParent(item.hwndItem)==window?pageBrush:paperBrush));
        shape(item.hDC,r,bg,edge,px(metric(cap?6:8)),active?std::max(1,px(2)):1);
        wstring title;
        if (cap) {
            const size_t i=static_cast<size_t>(id-KeyFirst);
            title=keys()[i].name;
            if (demo && page==Page::Keys && draft.mapping[i]!=applied.mapping[i]) {
                RECT marker=r;
                marker.left=marker.right-px(metric(13)); marker.bottom=marker.top+px(metric(15));
                SetBkMode(item.hDC,TRANSPARENT); SetTextColor(item.hDC,p.accent);
                const auto old=SelectObject(item.hDC,small);
                DrawTextW(item.hDC,L"*",1,&marker,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
                SelectObject(item.hDC,old);
            }
            if (demo && page==Page::Light && draft.light.effect!=0 && draft.light.brightness>0 && keys()[i].name!=L"Knob") {
                const auto& e=effects()[static_cast<size_t>(draft.light.effect)];
                const int n=e.color?draft.light.color:7;
                const auto color=rgb[static_cast<size_t>(n==7?i%rgb.size():n)];
                const auto blend=[this](int a,int b) { const int strength=2+draft.light.brightness; return (strength*a+(10-strength)*b)/10; };
                const auto b=CreateSolidBrush(RGB(blend(GetRValue(color),GetRValue(bg)),blend(GetGValue(color),GetGValue(bg)),blend(GetBValue(color),GetBValue(bg))));
                RECT line{r.left+px(metric(5)),r.bottom-px(metric(7)),r.right-px(metric(5)),r.bottom-px(metric(4))};
                FillRect(item.hDC,&line,b); DeleteObject(b);
            }
        } else {
            wchar_t titleBuffer[256]{};
            GetWindowTextW(item.hwndItem,titleBuffer,static_cast<int>(std::size(titleBuffer)));
            title=titleBuffer;
        }
        SetBkMode(item.hDC,TRANSPARENT);
        SetTextColor(item.hDC,primary?p.onAccent:disabled?p.quiet:accentText?p.accent:p.ink);
        const auto old=SelectObject(item.hDC,cap?small:regular);
        RECT tx=r; InflateRect(&tx,-px(metric(cap?2:4)),0);
        DrawTextW(item.hDC,title.c_str(),static_cast<int>(title.size()),&tx,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        SelectObject(item.hDC,old);
        if (item.itemState&ODS_FOCUS) { InflateRect(&r,-px(3),-px(3)); DrawFocusRect(item.hDC,&r); }
    }
    LRESULT controlColor(UINT msg,HDC dc,HWND h) const {
        const int id=GetDlgCtrlID(h);
        const bool panel=(page==Page::Light||page==Page::Keys) && GetParent(h)==view && (id==LayoutCaption||id==Legend||(id>=TaskTitle && id<=Help));
        const bool field=msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX || id==InputField;
        COLORREF ink=p.ink;
        if (id==DangerHead || (id==Footer && (demo?demoError:deviceError))) ink=p.danger;
        else if (id==Connection && demo) ink=p.accent;
        else if (id==Legend||id==Intro) ink=p.quiet;
        SetTextColor(dc,ink);
        SetBkColor(dc,panel||field?p.paper:p.bg);
        SetBkMode(dc,field?OPAQUE:TRANSPARENT);
        return reinterpret_cast<LRESULT>(panel||field?paperBrush:pageBrush);
    }

    static LRESULT CALLBACK ViewProc(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto* g=reinterpret_cast<Gui*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if (m==WM_NCCREATE) {
            g=static_cast<Gui*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(g));
        }
        if (!g) return DefWindowProcW(h,m,w,l);
        switch (m) {
        case WM_COMMAND: g->command(LOWORD(w),HIWORD(w)); return 0;
        case WM_DRAWITEM: g->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
        case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT:
            return g->controlColor(m,reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l));
        case WM_ERASEBKGND: {
            RECT r{}; GetClientRect(h,&r); FillRect(reinterpret_cast<HDC>(w),&r,g->pageBrush?g->pageBrush:GetSysColorBrush(COLOR_WINDOW));
            if (g->keyboardPanel.right>g->keyboardPanel.left) shape(reinterpret_cast<HDC>(w),g->keyboardPanel,g->p.paper,g->p.line,g->px(g->metric(10)));
            if (g->detailPanel.right>g->detailPanel.left) shape(reinterpret_cast<HDC>(w),g->detailPanel,g->p.paper,g->p.line,g->px(g->metric(10)));
            return 1;
        }
        case WM_VSCROLL: g->scroll(SB_VERT,LOWORD(w)); return 0;
        case WM_HSCROLL: g->scroll(SB_HORZ,LOWORD(w)); return 0;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: g->wheel(m,w); return 0;
        default: return DefWindowProcW(h,m,w,l);
        }
    }
    void wheel(UINT m, WPARAM w) {
        const auto d=GET_WHEEL_DELTA_WPARAM(w);
        const bool horizontal=m==WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(w)&MK_SHIFT);
        int& pos=horizontal?sx:sy;
        int delta=MulDiv(d,px(metric(75)),WHEEL_DELTA);
        pos+=m==WM_MOUSEHWHEEL?delta:-delta;
        layout();
    }
    static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto* g=reinterpret_cast<Gui*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if (m==WM_NCCREATE) {
            g=static_cast<Gui*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            g->window=h;
            SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(g));
        }
        if (!g) return DefWindowProcW(h,m,w,l);
        switch (m) {
        case WM_CREATE: return g->create()?0:-1;
        case WM_SIZE: if (w!=SIZE_MINIMIZED) g->layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* s=reinterpret_cast<MINMAXINFO*>(l);
            // The action bar remains outside the scrollable work area.
            s->ptMinTrackSize.x=g->px(680); s->ptMinTrackSize.y=g->px(460);
            return 0;
        }
        case WM_DPICHANGED: {
            const RECT& r=*reinterpret_cast<const RECT*>(l);
            g->dpi=HIWORD(w); g->appearance();
            SetWindowPos(h,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
            g->layout(); return 0;
        }
        case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE:
            g->appearance(); g->layout(); break;
        case WM_COMMAND: g->command(LOWORD(w),HIWORD(w)); return 0;
        case WM_DRAWITEM: g->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
        case WM_CTLCOLORSTATIC: return g->controlColor(m,reinterpret_cast<HDC>(w),reinterpret_cast<HWND>(l));
        case WM_ERASEBKGND: {
            RECT r{}; GetClientRect(h,&r); FillRect(reinterpret_cast<HDC>(w),&r,g->pageBrush?g->pageBrush:GetSysColorBrush(COLOR_WINDOW));
            HDC dc=reinterpret_cast<HDC>(w);
            HBRUSH line=CreateSolidBrush(g->p.line);
            const int top=g->px(g->metric(g->dip(r.right)<790?78:60));
            RECT a{0,top-1,r.right,top}; FillRect(dc,&a,line);
            RECT b{0,r.bottom-g->px(g->metric(60)),r.right,r.bottom-g->px(g->metric(60))+1}; FillRect(dc,&b,line);
            DeleteObject(line); return 1;
        }
        case WM_DEVICECHANGE:
            if (w==DBT_DEVICEARRIVAL || w==DBT_DEVICEREMOVECOMPLETE || w==DBT_DEVNODES_CHANGED) g->hotplug();
            return TRUE;
        case WM_TIMER:
            if (w==PollTimer) g->poll();
            if (w==RefreshTimer) {
                KillTimer(h,RefreshTimer);
                if (g->work==Work::None) g->begin(Work::Scan); else g->scanAgain=true;
            }
            return 0;
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: g->wheel(m,w); return 0;
        case FitFocus: g->reveal(reinterpret_cast<HWND>(w)); return 0;
        case WM_CLOSE: g->close(); return 0;
        case WM_DESTROY:
            if (g->notification) UnregisterDeviceNotification(g->notification);
            KillTimer(h,RefreshTimer); KillTimer(h,PollTimer);
            PostQuitMessage(0); return 0;
        default: break;
        }
        return DefWindowProcW(h,m,w,l);
    }
};

bool registerClasses(HINSTANCE instance) {
    WNDCLASSEXW w{sizeof(w)};
    w.hInstance=instance; w.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    w.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    w.lpfnWndProc=Gui::WindowProc; w.lpszClassName=MainClass;
    if (!RegisterClassExW(&w)) return false;
    w.hIcon=nullptr; w.lpfnWndProc=Gui::ViewProc; w.lpszClassName=ViewClass;
    return RegisterClassExW(&w)!=0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&common) || !registerClasses(instance)) return 1;
    Gui gui;
    gui.instance=instance;
    gui.smoke=arguments && std::wstring_view(arguments)==L"--smoke-test";
    const DWORD windowStyle=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
    RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
    const UINT dpi=GetDpiForSystem();
    RECT ideal{0,0,MulDiv(1180,dpi,96),MulDiv(720,dpi,96)};
    AdjustWindowRectExForDpi(&ideal,windowStyle,FALSE,0,dpi);
    const int width=std::min(static_cast<int>(ideal.right-ideal.left),static_cast<int>(area.right-area.left));
    const int height=std::min(static_cast<int>(ideal.bottom-ideal.top),static_cast<int>(area.bottom-area.top));
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,MainClass,L"F75 配置工具",windowStyle,
        area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,
        nullptr,nullptr,instance,&gui);
    if (!h) return 1;
    ShowWindow(h,gui.smoke?SW_SHOWNOACTIVATE:show);
    UpdateWindow(h);
    if (gui.smoke && !PostMessageW(h,WM_CLOSE,0,0)) return 1;
    MSG msg{};
    BOOL result;
    while ((result=GetMessageW(&msg,nullptr,0,0))>0) {
        if (msg.message==WM_KEYDOWN && (msg.wParam==VK_RETURN||msg.wParam==VK_ESCAPE)) {
            const HWND focus=GetFocus();
            const int id=focus?GetDlgCtrlID(focus):0;
            if (msg.wParam==VK_ESCAPE && id>0 && GetFocus() && IsChild(h,GetFocus())) {
                const bool drop=(id==Candidate||id==EffectBox||id==BrightBox||id==SpeedBox||id==ColorBox||id==KeyTarget)
                    && SendMessageW(focus,CB_GETDROPPEDSTATE,0,0)!=0;
                if (!drop) { SetFocus(gui.at(Nav+static_cast<int>(gui.page))); continue; }
            }
            if (msg.wParam==VK_RETURN) {
                wchar_t cls[16]{}; GetClassNameW(focus,cls,static_cast<int>(std::size(cls)));
                if (lstrcmpiW(cls,L"Button")==0) { SendMessageW(focus,BM_CLICK,0,0); continue; }
            }
        }
        if (!IsDialogMessageW(h,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return result<0?1:static_cast<int>(msg.wParam);
}
