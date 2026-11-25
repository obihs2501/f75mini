#include "snapshot.hpp"
#include <sstream>

namespace f75 {
std::wstring_view transport_name(Transport value) {
    return value == Transport::Wired ? L"USB-C 有线" : L"2.4G 接收器";
}
std::wstring_view connection_name(ConnectionState value) {
    switch (value) {
    case ConnectionState::Ready: return L"键盘已响应";
    case ConnectionState::ReceiverUnconfirmed: return L"接收器已连接，键盘未确认响应";
    case ConnectionState::Busy: return L"设备忙或访问被拒绝";
    case ConnectionState::Disconnected: return L"已断开";
    case ConnectionState::Unverified: return L"设备接口或协议待确认";
    default: return L"读取失败";
    }
}
std::wstring_view mode_name(SystemMode mode) {
    switch (mode) {
    case SystemMode::Android: return L"Android";
    case SystemMode::Windows: return L"Windows";
    case SystemMode::Mac: return L"Mac";
    default: return L"未知";
    }
}
std::string json_string(std::wstring_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const auto c : utf8(value)) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\' || c == '"') { result += '\\'; result += c; }
        else if (byte < 32) { result += "\\u00"; result += hex[byte >> 4]; result += hex[byte & 15]; }
        else result += c;
    }
    return result + '"';
}

std::string snapshot_json(const DeviceCandidate& c, const DeviceSnapshot& s) {
    const auto num = [](auto field) { return field ? std::to_string(*field) : std::string("null"); };
    const char* state = "error";
    switch (s.state) {
    case ConnectionState::Ready: state = "ready"; break;
    case ConnectionState::ReceiverUnconfirmed: state = "receiver_unconfirmed"; break;
    case ConnectionState::Busy: state = "busy"; break;
    case ConnectionState::Disconnected: state = "disconnected"; break;
    case ConnectionState::Unverified: state = "unverified"; break;
    default: break;
    }
    std::ostringstream out;
    out << "{\n  \"format\": \"f75-device-snapshot\",\n  \"version\": 1,\n  \"restorable\": false,\n  \"can_write\": false,\n  \"layout_verified\": false,\n  \"device\": {\"name\": " << json_string(c.name)
        << ", \"vid\": " << c.vid << ", \"pid\": " << c.pid << ", \"device_version\": " << c.version
        << ", \"transport\": \"" << (c.transport == Transport::Wired ? "wired" : "receiver") << "\"},\n"
        << "  \"state\": \"" << state << "\",\n  \"state_text\": " << json_string(connection_name(s.state))
        << ",\n  \"issue\": " << json_string(s.issue.message) << ",\n  \"lighting\": ";
    if (s.lighting) out << "{\"effect_id\": " << static_cast<int>(s.lighting->effect)
        << ", \"brightness\": " << num(s.lighting->brightness) << ", \"speed\": " << num(s.lighting->speed)
        << ", \"colour_index\": " << num(s.lighting->colour) << '}';
    else out << "null";
    out << ",\n  \"system_mode\": " << (s.system_mode ? json_string(mode_name(*s.system_mode)) : "null")
        << ",\n  \"sleep_seconds\": " << num(s.sleep_seconds) << ",\n  \"battery\": ";
    if (s.battery) out << "{\"power\": \"" << (s.battery->power == BatteryPower::OnBattery ? "battery" : "external")
        << "\", \"percent\": " << num(s.battery->percent) << '}';
    else out << "null";
    out << ",\n  \"base_layer_matrix\": ";
    if (!s.base_layer) out << "null";
    else {
        out << '[';
        bool first = true;
        for (const auto& k : *s.base_layer) {
            if (!first) out << ',';
            first = false;
            out << '[' << +k[0] << ',' << +k[1] << ',' << +k[2] << ',' << +k[3] << ']';
        }
        out << ']';
    }
    out << ",\n  \"unavailable\": [\"physical_key_mapping\", \"win_lock\", \"fn_layer\", \"per_key_rgb\", \"macros\", \"onboard_profiles\"";
    if (!s.lighting) out << ", \"lighting\"";
    if (!s.system_mode) out << ", \"system_mode\"";
    if (!s.sleep_seconds) out << ", \"sleep_seconds\"";
    if (!s.base_layer) out << ", \"base_layer_matrix\"";
    if (!s.battery) out << ", \"battery\"";
    out << "],\n"
        << "  \"details\": {\"lighting\": " << json_string(s.lighting_issue.message)
        << ", \"battery\": " << json_string(s.battery_issue.message)
        << ", \"base_layer\": " << json_string(s.base_layer_issue.message) << "}\n}\n";
    return out.str();
}

bool save_snapshot(const std::filesystem::path& path, const DeviceCandidate& device, const DeviceSnapshot& snapshot, std::wstring& error, bool replace) {
    if (snapshot.state != ConnectionState::Ready || (!snapshot.lighting && !snapshot.system_mode && !snapshot.sleep_seconds && !snapshot.base_layer)) {
        error = L"没有可导出的当前设备配置；旧快照、演示或仅电量信息不能作为配置导出。";
        return false;
    }
    return save_utf8(path, snapshot_json(device, snapshot), error, replace);
}
}
