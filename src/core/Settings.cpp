#include "core/Settings.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace tc {
using json = nlohmann::json;
namespace {
json Parse(const std::string& text) {
    if (text.empty()) return json::object();
    auto document = json::parse(text, nullptr, false);
    return document.is_object() ? document : json::object();
}
void MakeDirectories(const std::string& path) {
    size_t at = 1;
    while ((at = path.find('/', at)) != std::string::npos) { mkdir(path.substr(0, at).c_str(), 0755); ++at; }
}
}
Settings::Settings(std::string path) : fPath(std::move(path)), fDocument("{}") {}
std::string Settings::DefaultPath() {
    const char* home = getenv("HOME");
    return std::string(home && *home ? home : "/boot/home") + "/config/settings/TurboChook/settings.json";
}
bool Settings::Load() {
    std::ifstream in(fPath);
    if (!in) return false;
    std::stringstream buffer; buffer << in.rdbuf();
    fDocument = Parse(buffer.str()).dump();
    return true;
}
bool Settings::Save() const {
    MakeDirectories(fPath.substr(0, fPath.rfind('/') + 1));
    auto temporary = fPath + ".new";
    { std::ofstream out(temporary); if (!out) return false; out << Parse(fDocument).dump(2) << '\n'; }
    return rename(temporary.c_str(), fPath.c_str()) == 0;
}
std::string Settings::Get(const std::string& key, const std::string& fallback) const {
    auto document = Parse(fDocument);
    auto it = document.find(key);
    if (it == document.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}
int Settings::GetInt(const std::string& key, int fallback) const {
    auto document = Parse(fDocument);
    auto it = document.find(key);
    return it != document.end() && it->is_number() ? it->get<int>() : fallback;
}
bool Settings::GetBool(const std::string& key, bool fallback) const {
    auto document = Parse(fDocument);
    auto it = document.find(key);
    return it != document.end() && it->is_boolean() ? it->get<bool>() : fallback;
}
void Settings::Set(const std::string& key, const std::string& value) { auto d = Parse(fDocument); d[key] = value; fDocument = d.dump(); }
void Settings::SetInt(const std::string& key, int value) { auto d = Parse(fDocument); d[key] = value; fDocument = d.dump(); }
void Settings::SetBool(const std::string& key, bool value) { auto d = Parse(fDocument); d[key] = value; fDocument = d.dump(); }
std::vector<std::string> Settings::List(const std::string& key) const {
    std::vector<std::string> values;
    auto document = Parse(fDocument);
    auto it = document.find(key);
    if (it == document.end() || !it->is_array()) return values;
    for (const auto& entry : *it) if (entry.is_string()) values.push_back(entry.get<std::string>());
    return values;
}
void Settings::SetList(const std::string& key, const std::vector<std::string>& values) {
    auto d = Parse(fDocument); d[key] = values; fDocument = d.dump();
}
void Settings::TouchRepository(const std::string& path) {
    auto list = Repositories();
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    list.insert(list.begin(), path);
    if (list.size() > 30) list.resize(30);
    SetList("repositories", list);
}
void Settings::ForgetRepository(const std::string& path) {
    auto list = Repositories();
    list.erase(std::remove(list.begin(), list.end(), path), list.end());
    SetList("repositories", list);
}
}
