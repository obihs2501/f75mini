#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace f75 {

enum class Transport { Wired, Receiver };
enum class ConnectionState { Ready, ReceiverUnconfirmed, Busy, Disconnected, Unverified, Error };
enum class DeviceError { None, AccessDenied, Busy, Disconnected, Timeout, Unsupported, InvalidReply, System };

struct DeviceIssue {
    DeviceError code{DeviceError::None};
    std::uint32_t win32_error{};
    std::wstring message;
};

struct DeviceCandidate {
    std::wstring path; // Opaque, local to this machine; not a physical-device identifier.
    std::wstring name;
    Transport transport{Transport::Wired};
    std::uint16_t vid{};
    std::uint16_t pid{};
    std::uint16_t version{};
    DeviceIssue issue; // A path can be listed even when metadata could not be opened/verified.
};

enum class SystemMode { Android, Windows, Mac };
enum class BatteryPower { OnBattery, ExternalPower };
struct BatteryState {
    BatteryPower power{BatteryPower::OnBattery};
    std::optional<std::uint8_t> percent; // Not present on external power.
};

struct LightingState {
    std::uint8_t effect{}; // Raw effect ID; unknown IDs are not normalized to off.
    std::optional<std::uint8_t> brightness; // 0–9
    std::optional<std::uint8_t> speed;      // 0–4, protocol value
    std::optional<std::uint8_t> colour;     // Palette index 0–7, not arbitrary RGB
};

using KeyCode = std::array<std::uint8_t, 4>;
using KeyLayer = std::array<KeyCode, 127>; // Matrix entries, not an assertion of physical ANSI layout.

struct DeviceSnapshot {
    ConnectionState state{ConnectionState::Error};
    DeviceIssue issue;
    std::optional<LightingState> lighting;
    std::optional<SystemMode> system_mode;
    std::optional<unsigned> sleep_seconds; // Configured wireless sleep, 0 means disabled.
    std::optional<BatteryState> battery;
    std::optional<KeyLayer> base_layer;
    DeviceIssue lighting_issue;
    DeviceIssue battery_issue;
    DeviceIssue base_layer_issue;
    bool can_write{false}; // Never true in this release; no configuration write API.
};

struct DiscoveryResult {
    std::vector<DeviceCandidate> candidates; // Wired first. No protocol requests during discovery.
    DeviceIssue issue; // Enumeration error is distinct from an empty successful enumeration.
};

// Blocking; the caller owns background scheduling, serialization and device selection.
// Read only the selected path; no implicit fallback to a different physical keyboard.
[[nodiscard]] DiscoveryResult discover_devices();
[[nodiscard]] DeviceSnapshot read_device(const DeviceCandidate& candidate);

} // namespace f75
