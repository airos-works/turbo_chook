#pragma once
#include <string>
#include <vector>

namespace tc {
// JSON-backed application settings and the recent repository list. Portable
// so the host tests can exercise it; the UI passes the Haiku settings path.
class Settings {
public:
    explicit Settings(std::string path);
    bool Load();
    bool Save() const;
    std::string Get(const std::string& key, const std::string& fallback = {}) const;
    int GetInt(const std::string& key, int fallback = 0) const;
    bool GetBool(const std::string& key, bool fallback = false) const;
    void Set(const std::string& key, const std::string& value);
    void SetInt(const std::string& key, int value);
    void SetBool(const std::string& key, bool value);
    std::vector<std::string> List(const std::string& key) const;
    void SetList(const std::string& key, const std::vector<std::string>& values);
    // Recent repositories: most recent first, capped, without duplicates.
    void TouchRepository(const std::string& path);
    void ForgetRepository(const std::string& path);
    std::vector<std::string> Repositories() const { return List("repositories"); }
    const std::string& Path() const { return fPath; }
    static std::string DefaultPath();   // ~/config/settings/TurboChook/settings.json
private:
    std::string fPath, fDocument;
};
}
