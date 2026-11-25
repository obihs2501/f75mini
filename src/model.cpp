#include "model.hpp"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <climits>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace f75 {
namespace {
struct Placement { std::wstring_view name; int w = 4, gap = 0; bool editable = true; };
constexpr size_t file_limit = 8192;

bool valid(const Demo& demo) {
    const auto& l = demo.light;
    if (l.effect < 0 || l.effect >= static_cast<int>(effects().size()) ||
        l.brightness < 0 || l.brightness > 7 || l.speed < 0 || l.speed > 7 ||
        l.color < 0 || l.color > 7 || demo.mapping.size() != keys().size()) return false;
    for (size_t i = 0; i < keys().size(); ++i) {
        if (keys()[i].editable) {
            if (std::find(targets().begin(), targets().end(), demo.mapping[i]) == targets().end()) return false;
        } else if (demo.mapping[i] != keys()[i].name) return false;
    }
    return true;
}

std::optional<std::filesystem::path> local_file(const std::filesystem::path& path) {
    if (path.empty() || path.native().find(L'\0') != std::wstring::npos) return std::nullopt;
    const auto& raw = path.native();
    const auto sep = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    if (raw.size() >= 2 && sep(raw[0]) && sep(raw[1])) return std::nullopt;
    std::error_code ec;
    auto full = std::filesystem::absolute(path, ec).lexically_normal();
    const auto& text = full.native();
    if (ec || text.size() < 4 || text[1] != L':' || !sep(text[2]) ||
        text.find(L':', 2) != std::wstring::npos || !full.has_filename()) return std::nullopt;
    const auto drive = GetDriveTypeW(full.root_path().c_str());
    if (drive == DRIVE_REMOTE || drive == DRIVE_NO_ROOT_DIR || drive == DRIVE_UNKNOWN) return std::nullopt;
    if (full.filename().native().back() == L'.' || full.filename().native().back() == L' ') return std::nullopt;
    return full;
}

bool number(std::string_view src, int& value) {
    if (src.size() != 1) return false;
    const auto result = std::from_chars(src.data(), src.data() + src.size(), value);
    return result.ec == std::errc{} && result.ptr == src.data() + src.size();
}
}

const std::vector<Key>& keys() {
    static const auto layout = [] {
        std::vector<Key> r;
        const auto row = [&r](int y, std::initializer_list<Placement> cells) {
            int x = 0;
            for (const auto& k : cells) { x += k.gap; r.push_back({k.name, x, y, k.w, k.editable}); x += k.w; }
        };
        row(0, {{L"Esc"},{L"F1",4,2},{L"F2"},{L"F3"},{L"F4"},{L"F5",4,2},{L"F6"},{L"F7"},{L"F8"},{L"F9",4,2},{L"F10"},{L"F11"},{L"F12"},{L"Knob",4,2,false}});
        row(1, {{L"`"},{L"1"},{L"2"},{L"3"},{L"4"},{L"5"},{L"6"},{L"7"},{L"8"},{L"9"},{L"0"},{L"-"},{L"="},{L"Backspace",8},{L"Del"}});
        row(2, {{L"Tab",6},{L"Q"},{L"W"},{L"E"},{L"R"},{L"T"},{L"Y"},{L"U"},{L"I"},{L"O"},{L"P"},{L"["},{L"]"},{L"\\",6},{L"PgUp"}});
        row(3, {{L"Caps",7},{L"A"},{L"S"},{L"D"},{L"F"},{L"G"},{L"H"},{L"J"},{L"K"},{L"L"},{L";"},{L"'"},{L"Enter",9},{L"PgDn"}});
        row(4, {{L"LShift",9},{L"Z"},{L"X"},{L"C"},{L"V"},{L"B"},{L"N"},{L"M"},{L","},{L"."},{L"/"},{L"RShift",7},{L"Up"},{L"End"}});
        row(5, {{L"LCtrl",5},{L"LWin",5},{L"LAlt",5},{L"Space",25},{L"Fn",4,0,false},{L"RCtrl"},{L"Left",4,4},{L"Down"},{L"Right"}});
        return r;
    }();
    return layout;
}

const std::array<Effect, 5>& effects() {
    static const std::array<Effect, 5> modes{{
        {L"关闭", false, false}, {L"常亮", false, true}, {L"呼吸", true, true},
        {L"彩虹", true, false}, {L"流光", true, true}
    }};
    return modes;
}

const std::vector<std::wstring>& targets() {
    static const auto list = [] {
        std::vector<std::wstring> r{L"禁用"};
        for (const auto& key : keys()) if (key.editable) r.emplace_back(key.name);
        for (const auto* extra : {L"RAlt", L"RWin", L"Home", L"Insert", L"PrtSc", L"ScrollLock", L"Pause", L"Menu"}) r.emplace_back(extra);
        return r;
    }();
    return list;
}

Demo::Demo() { for (const auto& key : keys()) mapping.emplace_back(key.name); }

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw std::length_error("UTF-16 input too large");
    const int length = static_cast<int>(text.size());
    // Descriptor strings may contain broken UTF-16; Windows replaces isolated
    // surrogate code units rather than making the entire snapshot unprintable.
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (count == 0) throw std::system_error(GetLastError(), std::system_category());
    std::string result(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), count, nullptr, nullptr))
        throw std::system_error(GetLastError(), std::system_category());
    return result;
}

bool save_utf8(const std::filesystem::path& path, std::string_view contents, std::wstring& error, bool replace) {
    error.clear();
    static volatile LONG sequence = 0;
    const auto destination = local_file(path);
    if (!destination || contents.size() > 1024 * 1024) { error = L"请指定本地文件路径，且数据不能超过大小限制。"; return false; }
    auto temporary = *destination;
    temporary += L".f75-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(InterlockedIncrement(&sequence)) + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = L"无法创建输出文件（错误 " + std::to_wstring(GetLastError()) + L"）。"; return false; }
    DWORD written = 0;
    const auto size = static_cast<DWORD>(contents.size());
    const bool ok = WriteFile(file, contents.data(), size, &written, nullptr) && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (ok && MoveFileExW(temporary.c_str(), destination->c_str(), flags)) return true;
    const DWORD reason = ok ? GetLastError() : ERROR_WRITE_FAULT;
    DeleteFileW(temporary.c_str());
    error = !replace && (reason == ERROR_ALREADY_EXISTS || reason == ERROR_FILE_EXISTS)
        ? L"目标文件已存在，未覆盖；请选择新文件名。" : L"无法保存文件；原有文件未替换。";
    return false;
}

static bool save_demo_impl(const std::filesystem::path& path, const Demo& demo, std::wstring& error, bool replace) {
    if (!valid(demo)) { error = L"演示配置中的灯光或键位数值无效。"; return false; }
    std::ostringstream out;
    out << "F75-DEMO\t1\tF75-ANSI\nlight\t" << demo.light.effect << '\t' << demo.light.brightness << '\t' << demo.light.speed << '\t' << demo.light.color << '\n';
    for (size_t i = 0; i < keys().size(); ++i) out << utf8(keys()[i].name) << '\t' << utf8(demo.mapping[i]) << '\n';
    return save_utf8(path, out.str(), error, replace);
}
bool save_demo(const std::filesystem::path& path, const Demo& demo, std::wstring& error) { return save_demo_impl(path, demo, error, true); }
bool save_new_demo(const std::filesystem::path& path, const Demo& demo, std::wstring& error) { return save_demo_impl(path, demo, error, false); }

std::optional<Demo> load_demo(const std::filesystem::path& path, std::wstring& error) {
    error.clear();
    // Do not accept streams, devices, or arbitrarily large input. A size cap is
    // enforced on the read itself as well, independent of the file metadata.
    const auto source = local_file(path);
    if (!source) { error = L"请选择本地演示配置文件。"; return std::nullopt; }
    const HANDLE file = CreateFileW(source->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = L"无法打开演示配置文件。"; return std::nullopt; }
    LARGE_INTEGER size{};
    std::array<char, file_limit + 1> buffer{};
    DWORD count = 0;
    const bool ok = GetFileType(file) == FILE_TYPE_DISK && GetFileSizeEx(file, &size) &&
        size.QuadPart > 0 && size.QuadPart <= static_cast<LONGLONG>(file_limit) &&
        ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count == size.QuadPart;
    CloseHandle(file);
    if (!ok) { error = L"无法读取文件，或文件大小超过限制。"; return std::nullopt; }
    std::string_view raw(buffer.data(), count);
    if (raw.starts_with("\xef\xbb\xbf")) raw.remove_prefix(3);
    const auto read_line = [&raw]() -> std::string_view {
        const auto end = raw.find('\n');
        if (end == raw.npos) return {};
        auto line = raw.substr(0, end);
        raw.remove_prefix(end + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        return line;
    };
    if (read_line() != "F75-DEMO\t1\tF75-ANSI") { error = L"仅支持 F75 ANSI 演示配置 v1；真实设备快照不能导入。"; return std::nullopt; }
    Demo result;
    auto light = read_line();
    if (light.size() != 13 || !light.starts_with("light\t") || light[7] != '\t' || light[9] != '\t' || light[11] != '\t' ||
        !number(light.substr(6,1), result.light.effect) || !number(light.substr(8,1), result.light.brightness) ||
        !number(light.substr(10,1), result.light.speed) || !number(light.substr(12,1), result.light.color)) {
        error = L"灯光字段缺失或格式无效。"; return std::nullopt;
    }
    for (size_t i = 0; i < keys().size(); ++i) {
        const auto line = read_line();
        const auto sep = line.find('\t');
        if (sep == line.npos || line.substr(0, sep) != utf8(keys()[i].name)) { error = L"物理键列表不匹配、重复或缺失。"; return std::nullopt; }
        const auto mapping = line.substr(sep + 1);
        if (!keys()[i].editable) {
            if (mapping != utf8(keys()[i].name)) { error = L"保留键不可在演示中改写。"; return std::nullopt; }
        } else {
            const auto& list = targets();
            const auto target = std::find_if(list.begin(), list.end(), [mapping](const auto& w) { return utf8(w) == mapping; });
            if (target == list.end()) { error = L"键位目标不受支持。"; return std::nullopt; }
            result.mapping[i] = *target;
        }
    }
    if (!raw.empty() || !valid(result)) { error = L"配置存在多余数据或数值越界。"; return std::nullopt; }
    return result;
}
}
