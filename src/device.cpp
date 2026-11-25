#include "device.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#ifdef _MSC_VER
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#endif

// Independent implementation of published wire-format facts. In particular:
// - F75 Studio docs/PROTOCOL.md at 4f7cebe04ad6a70345e7d1df72c1ce17786c14c4;
//   no standalone upstream license located at that revision, no source ported.
// - Remap75 data/src/protocol.rs at 853f7e10f03977740eb7b2f2d465842813cac12e;
//   sanshoku aula/{canvas,wired,frame,decode,onboard}.go at b23c1bb65fd43bc0af1d306a6f20f78ab5f968ea.
// MIT notices for the two reference implementations:
// Copyright (c) 2026 rosman21
// Copyright (c) 2026 ushineko
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

namespace f75 {
namespace {
using Byte = std::uint8_t;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr Byte wired_report = 0x06;
constexpr Byte receiver_report = 0x13;
constexpr Byte f75_model = 0xcd;
constexpr std::size_t wired_size = 520;
constexpr std::size_t receiver_size = 20;
constexpr std::size_t receiver_chunk = 14;
constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE;
// Microsoft HIDClass user-mode feature I/O. Use the same codes as hidclass.h,
// without requiring a separate WDK include path. SET carries a GET header only.
constexpr DWORD set_feature = CTL_CODE(FILE_DEVICE_KEYBOARD, 100, METHOD_IN_DIRECT, FILE_ANY_ACCESS);
constexpr DWORD get_feature = CTL_CODE(FILE_DEVICE_KEYBOARD, 100, METHOD_OUT_DIRECT, FILE_ANY_ACCESS);

struct WinHandle {
    explicit WinHandle(HANDLE value) : value(value) {}
    ~WinHandle() { if (*this) CloseHandle(value); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    explicit operator bool() const { return value != nullptr && value != INVALID_HANDLE_VALUE; }
    HANDLE value;
};

struct DeviceList {
    explicit DeviceList(HDEVINFO value) : value(value) {}
    ~DeviceList() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
    DeviceList(const DeviceList&) = delete;
    DeviceList& operator=(const DeviceList&) = delete;
    HDEVINFO value;
};

struct Preparsed {
    Preparsed() = default;
    ~Preparsed() { if (value) HidD_FreePreparsedData(value); }
    Preparsed(const Preparsed&) = delete;
    Preparsed& operator=(const Preparsed&) = delete;
    PHIDP_PREPARSED_DATA value{};
};

DeviceIssue issue(DeviceError code, std::wstring message, DWORD error = 0) {
    return {code, error, std::move(message)};
}

DeviceIssue os_issue(DWORD error, const wchar_t* stage) {
    DeviceError code = DeviceError::System;
    switch (error) {
    case ERROR_ACCESS_DENIED: code = DeviceError::AccessDenied; break;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY: code = DeviceError::Busy; break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_NO_SUCH_DEVICE:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_DEV_NOT_EXIST: code = DeviceError::Disconnected; break;
    case ERROR_TIMEOUT:
    case ERROR_SEM_TIMEOUT: code = DeviceError::Timeout; break;
    }
    return issue(code, stage, error);
}

bool failed(const DeviceIssue& x) { return x.code != DeviceError::None; }

struct PathMatch { Transport link; USHORT vid; USHORT pid; };

std::optional<PathMatch> match_path(std::wstring_view path) {
    // Bound comparisons to whole Windows HID path components. Path checks are
    // merely a scan prefilter; actual attributes and capabilities are required.
    std::wstring lower(path);
    for (auto& c : lower) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    constexpr std::wstring_view prefix = L"\\\\?\\hid#";
    if (!lower.starts_with(prefix) || lower.find(L'\0') != std::wstring::npos) return {};
    std::wstring_view instance(lower);
    instance.remove_prefix(prefix.size());
    const auto end = instance.find(L'#');
    if (end == std::wstring_view::npos) return {};
    instance = instance.substr(0, end);
    const auto has = [instance](std::wstring_view part) {
        std::wstring_view rest = instance;
        for (;;) {
            const auto sep = rest.find(L'&');
            if (rest.substr(0, sep) == part) return true;
            if (sep == std::wstring_view::npos) return false;
            rest.remove_prefix(sep + 1);
        }
    };
    if (!has(L"mi_01")) return {};
    if (has(L"vid_258a") && has(L"pid_010c") && has(L"col06")) return PathMatch{Transport::Wired, 0x258a, 0x010c};
    if (has(L"vid_3554") && has(L"pid_fa09")) return PathMatch{Transport::Receiver, 0x3554, 0xfa09};
    return {};
}

bool has_report(PHIDP_PREPARSED_DATA data, HIDP_REPORT_TYPE type, Byte id, ULONG size) {
    std::array<char, wired_size> scratch{};
    return size <= scratch.size() &&
        HidP_InitializeReportForID(type, id, data, scratch.data(), size) == HIDP_STATUS_SUCCESS;
}

DeviceIssue check_collection(HANDLE handle, const PathMatch& expected, USHORT& version) {
    HIDD_ATTRIBUTES attrs{};
    attrs.Size = sizeof(attrs);
    if (!HidD_GetAttributes(handle, &attrs)) return os_issue(GetLastError(), L"无法读取 HID 设备标识。");
    if (attrs.VendorID != expected.vid || attrs.ProductID != expected.pid)
        return issue(DeviceError::Unsupported, L"HID 设备标识与候选路径不一致。");
    version = attrs.VersionNumber;

    Preparsed data;
    if (!HidD_GetPreparsedData(handle, &data.value)) return os_issue(GetLastError(), L"无法读取 HID 描述符。");
    HIDP_CAPS caps{};
    if (HidP_GetCaps(data.value, &caps) != HIDP_STATUS_SUCCESS)
        return issue(DeviceError::Unsupported, L"无法解析 HID 集合描述符。");
    const bool match = expected.link == Transport::Wired
        ? caps.UsagePage == 0xff00 && caps.FeatureReportByteLength == wired_size &&
          has_report(data.value, HidP_Feature, wired_report, caps.FeatureReportByteLength)
        : caps.UsagePage == 0xff02 && caps.InputReportByteLength == receiver_size &&
          caps.OutputReportByteLength == receiver_size &&
          has_report(data.value, HidP_Input, receiver_report, caps.InputReportByteLength) &&
          has_report(data.value, HidP_Output, receiver_report, caps.OutputReportByteLength);
    if (!match) return issue(DeviceError::Unsupported, L"HID 用途页、报告类型、编号或长度不匹配。");
    return {};
}

struct IoResult { DWORD size{}; DeviceIssue issue; };

// Wait at most until the protocol deadline, then cancel and reap before freeing
// the OVERLAPPED or the caller's buffer. Windows does not promise a separate
// deadline for cancellation completion; callers must stay off the UI thread.
template<class Start>
IoResult perform(HANDLE handle, Clock::time_point deadline, Start start) {
    if (Clock::now() >= deadline) return {0, issue(DeviceError::Timeout, L"等待 HID 回复超时。")};
    WinHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return {0, os_issue(GetLastError(), L"无法创建 HID I/O 事件。")};
    OVERLAPPED op{};
    op.hEvent = event.value;
    DWORD n = 0;
    if (start(&op, &n)) return {n, {}};
    const auto start_error = GetLastError();
    if (start_error != ERROR_IO_PENDING) return {0, os_issue(start_error, L"HID I/O 请求失败。")};
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now());
    const auto waited = WaitForSingleObject(event.value, left.count() > 0 ? static_cast<DWORD>(left.count()) : 0);
    if (waited == WAIT_OBJECT_0) {
        if (GetOverlappedResult(handle, &op, &n, FALSE)) return {n, {}};
        const auto err = GetLastError();
        if (err != ERROR_IO_INCOMPLETE) return {0, os_issue(err, L"HID I/O 未成功完成。")};
    }
    const auto wait_error = waited == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
    (void)CancelIoEx(handle, &op);
    // No other code touches this handle/operation while it is pending.
    (void)GetOverlappedResult(handle, &op, &n, TRUE);
    return {0, waited == WAIT_TIMEOUT ? issue(DeviceError::Timeout, L"等待 HID 回复超时。")
                                     : os_issue(wait_error, L"等待 HID I/O 事件失败。")};
}

enum class Query { Model, Lighting, BaseLayer, Battery };

DeviceIssue wired_query(HANDLE handle, Query q, std::span<Byte> out) {
    // Whitelist semantic getters only; the model request is the only nonzero
    // selector, and callers cannot pass a raw command or payload.
    Byte command{}, selector{};
    switch (q) {
    case Query::Model: command = 0x82; selector = 1; break;
    case Query::Lighting: command = 0x84; break;
    case Query::BaseLayer: command = 0x83; break;
    case Query::Battery: return issue(DeviceError::Unsupported, L"有线通路没有已知电量读取依据。");
    }
    const auto deadline = Clock::now() + 1100ms;
    std::array<Byte, wired_size> request{};
    request[0] = wired_report;
    request[1] = command;
    request[2] = selector;
    request[4] = 1;
    request[6] = static_cast<Byte>(out.size() & 0xff);
    request[7] = static_cast<Byte>(out.size() >> 8);
    const auto sent = perform(handle, deadline, [&](OVERLAPPED* op, DWORD* n) {
        return DeviceIoControl(handle, set_feature, request.data(), static_cast<DWORD>(request.size()),
                               nullptr, 0, n, op);
    });
    if (failed(sent.issue)) return sent.issue;
    Sleep(50); // sanshoku's measured settle interval; the deadline is not extended.
    std::array<Byte, wired_size> answer{};
    answer[0] = wired_report;
    const auto got = perform(handle, deadline, [&](OVERLAPPED* op, DWORD* n) {
        // The output buffer supplies the ID; using it as the input as well
        // agrees with HIDAPI's Windows convention for counted feature replies.
        const auto length = static_cast<DWORD>(answer.size());
        return DeviceIoControl(handle, get_feature, answer.data(), length, answer.data(), length, n, op);
    });
    if (failed(got.issue)) return got.issue;
    // Require the full logical payload and the echoed feature header. Some
    // variants may include padding up to the descriptor length, never beyond it.
    if (got.size < 8 + out.size() || got.size > answer.size() ||
        !std::equal(request.begin(), request.begin() + 8, answer.begin()))
        return issue(DeviceError::InvalidReply, L"有线回复的报告长度或回显头与查询不符。");
    std::copy_n(answer.begin() + 8, out.size(), out.begin());
    return {};
}

using Packet = std::array<Byte, receiver_size>;

constexpr Byte checksum(const Packet& p) {
    unsigned sum = 0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) sum += p[i];
    return static_cast<Byte>(sum & 0xff);
}

DeviceIssue receiver_query(HANDLE handle, Query q, std::span<Byte> out) {
    Byte command{};
    switch (q) {
    case Query::Model: command = 0x05; break;
    case Query::Lighting: command = 0x44; break;
    case Query::BaseLayer: command = 0x41; break;
    case Query::Battery: command = 0x4a; break;
    }
    const auto deadline = Clock::now() + ((q == Query::Model || q == Query::Battery) ? 600ms : 1500ms);
    Packet request{};
    request[0] = receiver_report;
    request[1] = command;
    // The community battery capture uniquely uses total=0 for its query.
    request[2] = q == Query::Battery ? 0 : 1;
    request.back() = checksum(request);
    const auto sent = perform(handle, deadline, [&](OVERLAPPED* op, DWORD* n) {
        return WriteFile(handle, request.data(), static_cast<DWORD>(request.size()), n, op);
    });
    if (failed(sent.issue)) return sent.issue;
    if (sent.size != request.size()) return issue(DeviceError::InvalidReply, L"无线查询报告未完整送达 HID 通路。");

    // One new handle per snapshot; one request per command, no retries or mixing
    // attempts. The protocol has no nonce; an external client using the same
    // command concurrently cannot be distinguished with certainty.
    std::array<bool, (512 + receiver_chunk - 1) / receiver_chunk> seen{};
    const auto packets = (out.size() + receiver_chunk - 1) / receiver_chunk;
    std::size_t filled = 0;
    while (filled < packets) {
        Packet answer{};
        const auto got = perform(handle, deadline, [&](OVERLAPPED* op, DWORD* n) {
            return ReadFile(handle, answer.data(), static_cast<DWORD>(answer.size()), n, op);
        });
        if (failed(got.issue)) return got.issue;
        if (got.size < 2 || answer[0] != receiver_report || (answer[1] & 0x7f) != command) continue;
        // A short/invalid related report cannot become a successful reading.
        if (got.size != answer.size() || checksum(answer) != answer.back() ||
            (q != Query::Battery && (answer[1] & 0x80) != 0) || answer[2] != packets ||
            answer[3] >= packets || (answer[4] >> 4) != 0)
            return issue(DeviceError::InvalidReply, L"相关无线回复的长度、状态、校验和或分包头无效。");
        const auto index = static_cast<std::size_t>(answer[3]);
        const auto offset = index * receiver_chunk;
        const auto length = std::min(receiver_chunk, out.size() - offset);
        if ((answer[4] & 0x0fu) != length)
            return issue(DeviceError::InvalidReply, L"无线回复的分包长度与查询不符。");
        if (seen[index]) {
            if (!std::equal(answer.begin() + 5, answer.begin() + 5 + length, out.begin() + offset))
                return issue(DeviceError::InvalidReply, L"收到内容不一致的无线重复分包。");
            continue;
        }
        std::copy_n(answer.begin() + 5, length, out.begin() + offset);
        seen[index] = true;
        ++filled;
    }
    return {};
}

void decode_led(DeviceSnapshot& snapshot, const std::array<Byte, 128>& b) {
    if (b[126] != 0x5a || b[127] != 0xa5) {
        snapshot.lighting_issue = issue(DeviceError::InvalidReply, L"灯光设置块缺少结构签名 5A A5。");
        return;
    }
    switch (b[27]) {
    case 0: snapshot.system_mode = SystemMode::Windows; break;
    case 1: snapshot.system_mode = SystemMode::Android; break;
    case 2: snapshot.system_mode = SystemMode::Mac; break;
    }
    switch (b[0x18]) {
    case 0: case 2: case 3: case 4: case 6: case 8:
    case 10: case 20: case 30: case 40:
        snapshot.sleep_seconds = static_cast<unsigned>(b[0x18]) * 30u;
        break;
    }
    if (b[9] > 1) {
        snapshot.lighting_issue = issue(DeviceError::InvalidReply, L"灯光设置块含未知的自定义模式标志。");
        return;
    }
    LightingState state{};
    state.effect = b[9] == 1 ? 21 : b[10];
    if (state.effect >= 1 && state.effect <= 18) {
        // For built-in effects 1..18, both community descriptions agree on
        // their offsets. They disagree around custom/off; no parameters there.
        const auto at = 0x38 + 2 * static_cast<std::size_t>(state.effect);
        const auto speed = static_cast<Byte>(b[at + 1] >> 4);
        const auto colour = static_cast<Byte>(b[at + 1] & 0x0f);
        if (b[at] <= 9) state.brightness = b[at];
        if (state.effect != 1 && speed <= 4) state.speed = speed;
        if (state.effect != 3 && state.effect != 15 && state.effect != 16 &&
            state.effect != 17 && colour <= 7) state.colour = colour;
    }
    snapshot.lighting = state;
}

ConnectionState connection_error(const DeviceIssue& problem, Transport transport, bool receiver_probe) {
    switch (problem.code) {
    case DeviceError::Busy: return ConnectionState::Busy;
    case DeviceError::Disconnected: return ConnectionState::Disconnected;
    case DeviceError::Unsupported:
    case DeviceError::InvalidReply: return ConnectionState::Unverified;
    case DeviceError::Timeout:
        return receiver_probe && transport == Transport::Receiver ? ConnectionState::ReceiverUnconfirmed
                                                                  : ConnectionState::Error;
    default: return ConnectionState::Error;
    }
}

} // namespace

DiscoveryResult discover_devices() {
    DiscoveryResult result;
    GUID guid{};
    HidD_GetHidGuid(&guid);
    DeviceList devices(SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.value == INVALID_HANDLE_VALUE) {
        result.issue = os_issue(GetLastError(), L"无法初始化 HID 设备枚举。");
        return result;
    }
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA row{};
        row.cbSize = sizeof(row);
        if (!SetupDiEnumDeviceInterfaces(devices.value, nullptr, &guid, i, &row)) {
            const auto error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) result.issue = os_issue(error, L"HID 设备枚举中断。");
            break;
        }
        DWORD needed = 0;
        const auto measured = SetupDiGetDeviceInterfaceDetailW(devices.value, &row, nullptr, 0, &needed, nullptr);
        const auto size_error = GetLastError();
        constexpr auto path_offset = offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath);
        if (measured || size_error != ERROR_INSUFFICIENT_BUFFER ||
            needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || needed > 128 * 1024) {
            if (!failed(result.issue)) result.issue = os_issue(size_error, L"无法确定一个 HID 路径所需的空间。");
            continue;
        }
        // operator new via array-new supplies alignment for the variable-sized
        // SDK struct; read the flexible string only within the returned extent.
        auto storage = std::make_unique<std::byte[]>(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.get());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.value, &row, detail, needed, nullptr, nullptr)) {
            if (!failed(result.issue)) result.issue = os_issue(GetLastError(), L"无法取得一个 HID 接口路径。");
            continue;
        }
        const auto* first = reinterpret_cast<const wchar_t*>(storage.get() + path_offset);
        const auto* past = first + (needed - path_offset) / sizeof(wchar_t);
        const auto* nul = std::find(first, past, L'\0');
        if (nul == past) continue;
        const std::wstring path(first, nul);
        const auto expected = match_path(path);
        if (!expected) continue;

        DeviceCandidate candidate;
        candidate.path = path;
        candidate.transport = expected->link;
        candidate.vid = expected->vid;
        candidate.pid = expected->pid;
        candidate.name = expected->link == Transport::Wired ? L"F75 有线候选" : L"F75 2.4G 接收器候选";
        // Desired access 0: inspect metadata only, no reads/writes of reports.
        WinHandle handle(CreateFileW(path.c_str(), 0, sharing, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!handle) {
            const auto err = GetLastError();
            candidate.issue = os_issue(err, L"无法打开候选 HID 集合以验证描述符。");
        } else {
            candidate.issue = check_collection(handle.value, *expected, candidate.version);
        }
        // A rejected HID collection on the receiver's composite interface is
        // not a distinct keyboard candidate. Skip also if it raced removal.
        if (candidate.issue.code != DeviceError::Unsupported && candidate.issue.code != DeviceError::Disconnected)
            result.candidates.push_back(std::move(candidate));
    }
    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) {
        if (a.transport != b.transport) return a.transport == Transport::Wired;
        if (failed(a.issue) != failed(b.issue)) return !failed(a.issue);
        return a.path < b.path;
    });
    return result;
}

DeviceSnapshot read_device(const DeviceCandidate& candidate) {
    DeviceSnapshot snapshot;
    const auto stop = [&](DeviceIssue problem, bool receiver_probe = false) {
        // Do not present data from a just-disconnected snapshot as current.
        DeviceSnapshot result;
        result.state = connection_error(problem, candidate.transport, receiver_probe);
        result.issue = std::move(problem);
        return result;
    };
    const auto expected = match_path(candidate.path);
    if (!expected || expected->link != candidate.transport || expected->vid != candidate.vid || expected->pid != candidate.pid)
        return stop(issue(DeviceError::Unsupported, L"候选 HID 路径与已知接口不匹配。"));
    WinHandle handle(CreateFileW(candidate.path.c_str(), GENERIC_READ | GENERIC_WRITE, sharing,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (!handle) return stop(os_issue(GetLastError(), L"无法打开所选 HID 集合。"));
    USHORT version{};
    if (auto check = check_collection(handle.value, *expected, version); failed(check)) return stop(std::move(check));
    if (candidate.version != 0 && candidate.version != version)
        return stop(issue(DeviceError::Unsupported, L"所选 HID 设备版本已变化，请重新发现并选择。"));

    const bool wired = expected->link == Transport::Wired;
    const auto query = [&](Query q, std::span<Byte> out) {
        return wired ? wired_query(handle.value, q, out) : receiver_query(handle.value, q, out);
    };
    std::array<Byte, 10> model{};
    auto model_payload = std::span<Byte>(model).first(wired ? 6 : 10);
    if (auto response = query(Query::Model, model_payload); failed(response)) {
        if (!wired && response.code == DeviceError::Timeout)
            response.message = L"接收器可打开，但未获得键盘型号回复。请唤醒键盘、确认 2.4G 模式和供电后重试。";
        return stop(std::move(response), true);
    }
    if (model_payload[5] != f75_model)
        return stop(issue(DeviceError::Unsupported, L"设备回复的型号字节不是已知 F75 型号。"));
    // This establishes a responsive, protocol-matching model, not its layout,
    // a validated firmware variant, or permission to write settings.
    snapshot.state = ConnectionState::Ready;

    std::array<Byte, 128> led{};
    snapshot.lighting_issue = query(Query::Lighting, led);
    if (snapshot.lighting_issue.code == DeviceError::Disconnected) return stop(snapshot.lighting_issue);
    if (!failed(snapshot.lighting_issue)) decode_led(snapshot, led);

    std::array<Byte, 512> matrix{};
    snapshot.base_layer_issue = query(Query::BaseLayer, matrix);
    if (snapshot.base_layer_issue.code == DeviceError::Disconnected) return stop(snapshot.base_layer_issue);
    if (!failed(snapshot.base_layer_issue)) {
        if (matrix[508] != 0 || matrix[509] != 0 || matrix[510] != 0x5a || matrix[511] != 0xa5) {
            snapshot.base_layer_issue = issue(DeviceError::InvalidReply, L"基础键位块缺少尾部结构签名 00 00 5A A5。");
        } else {
            KeyLayer keys{};
            for (std::size_t i = 0; i < keys.size(); ++i)
                std::copy_n(matrix.begin() + 4 * i, 4, keys[i].begin());
            snapshot.base_layer = keys;
        }
    }

    if (wired) {
        snapshot.battery_issue = issue(DeviceError::Unsupported, L"当前有线通路没有已知电量读取依据。");
    } else {
        std::array<Byte, 2> power{};
        snapshot.battery_issue = query(Query::Battery, power);
        if (snapshot.battery_issue.code == DeviceError::Disconnected) return stop(snapshot.battery_issue);
        if (!failed(snapshot.battery_issue)) {
            if (power[1] == 1 && power[0] >= 1 && power[0] <= 100) {
                snapshot.battery = BatteryState{BatteryPower::OnBattery, power[0]};
            } else if (power[1] == 0x10 && power[0] == 100) {
                snapshot.battery = BatteryState{BatteryPower::ExternalPower, std::nullopt};
            } else {
                snapshot.battery_issue = issue(DeviceError::InvalidReply, L"电量回复包含未知的电源状态或百分比。");
            }
        }
    }
    return snapshot;
}

} // namespace f75
