#include "snapshot.hpp"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <set>

namespace {
int errors = 0;
void check(bool condition, const char* message) { if (!condition) { ++errors; std::cerr << message << '\n'; } }
}

int main() {
    using namespace f75;
    const auto path = std::filesystem::temp_directory_path() / (L"f75-model-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::wstring error;
    const auto& layout = keys();
    check(layout.size() == 81, "ANSI must be 80 keys and one knob");
    std::set<std::wstring_view> unique;
    for (size_t i = 0; i < layout.size(); ++i) {
        const auto& key = layout[i];
        check(unique.insert(key.name).second, "duplicate key");
        check(key.w > 0 && key.x >= 0 && key.x + key.w <= 64 && key.y >= 0 && key.y <= 5, "layout bounds");
        for (size_t j = i + 1; j < layout.size(); ++j) {
            const auto& other = layout[j];
            check(key.y != other.y || key.x + key.w <= other.x || other.x + other.w <= key.x, "overlapping keys");
        }
    }
    Demo demo;
    demo.mapping[0] = L"禁用"; demo.light.color = 7;
    check(save_new_demo(path, demo, error), "initial save failed");
    check(!save_new_demo(path, Demo{}, error), "no-clobber overwrote old file");
    check(load_demo(path, error) == demo, "roundtrip changed demo");
    demo.light.effect = 99;
    check(!save_demo(path, demo, error), "invalid effect accepted");
    demo.light.effect = 0;
    const auto fn = std::ranges::find_if(layout, [](const auto& k) { return k.name == L"Fn"; });
    if (fn != layout.end()) demo.mapping[static_cast<size_t>(fn-layout.begin())] = L"A";
    check(!save_demo(path, demo, error), "reserved key accepted");
    check(save_utf8(path, "F75-DEMO\t1\tF75-ISO\nlight\t0\t5\t0\t0\n", error), "invalid fixture write");
    check(!load_demo(path, error), "foreign layout accepted");
    check(save_utf8(path, "F75-DEMO\t1\tF75-ANSI\nlight\t0\t5\t0\t0\nEsc\tA\nEsc\tA\n", error), "duplicate fixture write");
    check(!load_demo(path, error), "duplicate/truncated mapping accepted");
    check(save_utf8(path, std::string(8193, 'a'), error), "oversize fixture write");
    check(!load_demo(path, error), "oversize input accepted");
    check(save_demo(path, Demo{}, error), "default fixture write failed");
    {
        std::string full = "F75-DEMO\t1\tF75-ANSI\nlight\t9\t5\t0\t0\n";
        for (const auto& k : keys()) full += utf8(k.name) + '\t' + utf8(k.name) + '\n';
        check(save_utf8(path, full, error), "numeric fixture write failed");
        check(!load_demo(path, error), "out-of-range light accepted");
    }
    DeviceCandidate c; c.name = L"F75 \"test\"\n"; c.path = L"private_device_path";
    DeviceSnapshot s;
    check(!save_snapshot(path, c, s, error), "error snapshot exported");
    s.state = ConnectionState::Ready;
    s.battery = BatteryState{BatteryPower::ExternalPower, std::nullopt};
    check(!save_snapshot(path, c, s, error), "battery-only response exported as config");
    s.lighting = LightingState{1, std::uint8_t{0}, std::nullopt, std::nullopt};
    check(save_snapshot(path, c, s, error), "valid snapshot rejected");
    check(!load_demo(path, error), "hardware snapshot imported into demo");
    const auto json = snapshot_json(c, s);
    check(json.find("private_device_path") == json.npos, "private path leaked");
    check(json.find("\\\"test\\\"\\u000a") != json.npos && json.find("\"brightness\": 0") != json.npos && json.find("\"percent\": null") != json.npos, "JSON escaping or null/zero representation wrong");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return errors ? 1 : 0;
}
