#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace f75 {
struct Key { std::wstring_view name; int x, y, w; bool editable; };
const std::vector<Key>& keys();

struct Light {
    int effect = 1, brightness = 5, speed = 3, color = 0;
    bool operator==(const Light&) const = default;
};
struct Effect { std::wstring_view name; bool speed, color; };
const std::array<Effect, 5>& effects();

struct Demo {
    Light light;
    std::vector<std::wstring> mapping;
    Demo();
    bool operator==(const Demo&) const = default;
};
const std::vector<std::wstring>& targets();
bool save_demo(const std::filesystem::path&, const Demo&, std::wstring& error);
std::optional<Demo> load_demo(const std::filesystem::path&, std::wstring& error);

// Atomic UTF-8 file output; a failed replacement leaves the prior file intact.
bool save_utf8(const std::filesystem::path&, std::string_view, std::wstring& error, bool replace = true);
bool save_new_demo(const std::filesystem::path&, const Demo&, std::wstring& error);
std::string utf8(std::wstring_view);
}
