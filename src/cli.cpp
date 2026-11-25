#include "snapshot.hpp"
#include <windows.h>
#include <cstdio>
#include <charconv>

namespace {
using namespace f75;

void print(std::wstring_view message, bool error = false) {
    const HANDLE output = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode{}, written{};
    if (GetConsoleMode(output, &mode)) WriteConsoleW(output, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    else {
        const auto text = utf8(message);
        std::fwrite(text.data(), 1, text.size(), error ? stderr : stdout);
    }
}

int fail(std::wstring_view message, int status) {
    print(message, true); print(L"\n", true); return status;
}

constexpr auto help = LR"(F75 配置工具 · 命令行（真实设备只读）

  f75-cli list [--json]
  f75-cli info --device N [--json]
  f75-cli export --device N --out 文件.json
  f75-cli demo create 新文件.f75demo
  f75-cli demo validate 文件.f75demo

先用 list 查看设备编号。只有一台候选设备时，info 可省略 --device。
编号从 1 开始，按本次枚举计算；拔插后请重新 list。
导出不覆盖已有文件；硬件写入与恢复未开放。
退出码：0 成功，2 参数错误，3 无法选择设备，4 读取未就绪，5 文件错误，6 内部错误。
)";

int run(int argc, wchar_t** argv) {
    if (argc == 1 || (argc == 2 && (std::wstring_view(argv[1]) == L"--help" || std::wstring_view(argv[1]) == L"-h"))) { print(help); return 0; }
    const std::wstring_view command = argv[1];
    if (command == L"demo") {
        if (argc != 4) return fail(L"请使用 demo create 或 demo validate，并指定文件路径。", 2);
        const std::wstring_view action = argv[2];
        std::wstring error;
        if (action == L"create") {
            const std::filesystem::path path = argv[3];
            if (!save_new_demo(path, Demo{}, error)) return fail(error, 5);
            print(L"已生成演示配置；不包含真实设备数据。\n"); return 0;
        }
        if (action == L"validate") {
            if (!load_demo(argv[3], error)) return fail(error, 5);
            print(L"演示配置有效；未向键盘写入。\n"); return 0;
        }
        return fail(L"未知演示命令。", 2);
    }
    if (command != L"list" && command != L"info" && command != L"export")
        return fail(L"未知命令；请使用 --help。未经真机验收的写入未开放。", 2);
    bool json = false;
    std::optional<unsigned> id;
    std::optional<std::filesystem::path> out;
    for (int i = 2; i < argc; ++i) {
        const std::wstring_view arg = argv[i];
        if (arg == L"--json" && command != L"export" && !json) json = true;
        else if (arg == L"--device" && command != L"list" && !id && i + 1 < argc) {
            const std::wstring_view value = argv[++i];
            if (value.empty() || value.size() > 9 || value.find_first_not_of(L"0123456789") != value.npos) return fail(L"设备编号须为正整数。", 2);
            unsigned n = 0;
            for (auto digit : value) n = n * 10 + static_cast<unsigned>(digit - L'0');
            if (!n) return fail(L"设备编号从 1 开始。", 2);
            id = n;
        } else if (arg == L"--out" && command == L"export" && !out && i + 1 < argc && *argv[i + 1]) out = argv[++i];
        else return fail(L"参数错误；请使用 --help。", 2);
    }
    if (command == L"export" && (!id || !out)) return fail(L"导出需要明确指定 --device N 和 --out 文件。", 2);
    const auto discovery = discover_devices();
    const auto& list = discovery.candidates;
    if (discovery.issue.code != DeviceError::None) return fail(discovery.issue.message, 3);
    if (command == L"list") {
        if (json) {
            std::fputs("[", stdout);
            for (size_t i = 0; i < list.size(); ++i) {
                const auto& c = list[i];
                std::printf("%s{\"id\":%zu,\"name\":%s,\"vid\":%u,\"pid\":%u,\"transport\":\"%s\",\"issue\":%s}", i ? "," : "", i + 1,
                    json_string(c.name).c_str(), static_cast<unsigned>(c.vid), static_cast<unsigned>(c.pid),
                    c.transport == Transport::Wired ? "wired" : "receiver", json_string(c.issue.message).c_str());
            }
            std::fputs("]\n", stdout);
        } else if (list.empty()) print(L"未发现 AULA F75 候选接口；请连接 USB-C 或 2.4G 接收器。\n");
        else for (size_t i = 0; i < list.size(); ++i) {
            const auto& c = list[i];
            print(std::to_wstring(i + 1) + L"  " + c.name + L" · " + std::wstring(transport_name(c.transport)) + L"（尚未查询）\n");
            if (!c.issue.message.empty()) print(c.issue.message + L"\n");
        }
        return 0;
    }
    if (list.empty()) return fail(L"未发现 AULA F75 候选接口。", 3);
    if ((!id && list.size() != 1) || (id && *id > list.size())) return fail(L"请用 list 核对编号，并用 --device N 明确选择。", 3);
    const auto& selected = list[id.value_or(1) - 1];
    const auto snapshot = read_device(selected);
    if (command == L"export") {
        if (snapshot.state != ConnectionState::Ready) return fail(snapshot.issue.message.empty() ? connection_name(snapshot.state) : std::wstring_view(snapshot.issue.message), 4);
        std::wstring error;
        if (!save_snapshot(*out, selected, snapshot, error, false)) return fail(error, 5);
        print(L"已导出可读取配置；这不是完整备份，不能恢复到键盘。\n");
        return 0;
    }
    if (json) std::fputs(snapshot_json(selected, snapshot).c_str(), stdout);
    else {
        print(selected.name + L" · " + std::wstring(transport_name(selected.transport)) + L"\n");
        print(std::wstring(connection_name(snapshot.state)) + L" · 本版只读\n");
        if (!snapshot.issue.message.empty()) print(snapshot.issue.message + L"\n");
        if (snapshot.lighting) {
            const auto& l = *snapshot.lighting;
            print(L"灯效 ID：" + std::to_wstring(l.effect) + L"\n");
            if (l.brightness) print(L"亮度：" + std::to_wstring(*l.brightness) + L"\n");
            if (l.speed) print(L"速度：" + std::to_wstring(*l.speed) + L"\n");
            if (l.colour) print(L"颜色索引：" + std::to_wstring(*l.colour) + L"\n");
        } else print(L"灯光：不可读取" + (snapshot.lighting_issue.message.empty() ? std::wstring{} : L" · " + snapshot.lighting_issue.message) + L"\n");
        print(L"系统模式：" + std::wstring(snapshot.system_mode ? mode_name(*snapshot.system_mode) : L"不可读取") + L"\n");
        print(L"Win 键固件锁定：无法读取。Windows 模式下可按手册用 Fn+Win 检查。\n");
        if (snapshot.sleep_seconds) print(*snapshot.sleep_seconds ? L"无线休眠：" + std::to_wstring(*snapshot.sleep_seconds) + L" 秒\n" : L"无线休眠：禁用\n");
        if (snapshot.battery && snapshot.battery->percent) print(L"电量：" + std::to_wstring(*snapshot.battery->percent) + L"%\n");
        else if (snapshot.battery && snapshot.battery->power == BatteryPower::ExternalPower) print(L"电量：外部供电，无法确认百分比\n");
        else print(L"电量：当前不可读取" + (snapshot.battery_issue.message.empty() ? std::wstring{} : L" · " + snapshot.battery_issue.message) + L"\n");
        print(snapshot.base_layer ? L"基础层矩阵：已读取，尚未确认物理键对应关系。\n" : L"基础层矩阵：不可读取。\n");
    }
    return snapshot.state == ConnectionState::Ready ? 0 : 4;
}
}

int wmain(int argc, wchar_t** argv) {
    try { return run(argc, argv); }
    catch (...) { return fail(L"发生内部错误；没有自动重试。", 6); }
}
