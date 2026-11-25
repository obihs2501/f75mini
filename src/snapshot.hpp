#pragma once
#include "device.hpp"
#include "model.hpp"

namespace f75 {
std::wstring_view transport_name(Transport);
std::wstring_view connection_name(ConnectionState);
std::wstring_view mode_name(SystemMode);
std::string json_string(std::wstring_view);
// Diagnostic data only. This format is deliberately incompatible with demo import.
std::string snapshot_json(const DeviceCandidate&, const DeviceSnapshot&);
bool save_snapshot(const std::filesystem::path&, const DeviceCandidate&, const DeviceSnapshot&, std::wstring& error, bool replace = true);
}
