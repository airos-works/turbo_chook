#include "core/GitExecutable.h"
#include "core/Process.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace tc {
namespace {
bool Executable(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(path.c_str(), X_OK) == 0;
}
std::string RealPath(const std::string& path) {
    char buffer[PATH_MAX];
    if (realpath(path.c_str(), buffer)) return buffer;
    return path;
}
std::string Home() {
    const char* home = getenv("HOME");
    return home && *home ? home : "/boot/home";
}
}
std::string GitInstallation::label() const {
    std::string text = version.empty() ? "unavailable" : "Git " + version;
    return text + " (" + origin + ") " + path;
}
std::vector<std::string> GitSearchDirectories(const char* pathEnvironment) {
    std::vector<std::string> directories = {
        "/boot/system/bin",
        "/boot/system/non-packaged/bin",
        Home() + "/config/non-packaged/bin",
        Home() + "/config/bin",
        "/usr/local/bin", "/usr/bin", "/bin"};
    std::string paths = pathEnvironment ? pathEnvironment : (getenv("PATH") ? getenv("PATH") : "");
    size_t start = 0;
    while (start <= paths.size() && !paths.empty()) {
        size_t end = paths.find(':', start);
        if (end == std::string::npos) end = paths.size();
        auto directory = paths.substr(start, end - start);
        if (!directory.empty() && std::find(directories.begin(), directories.end(), directory) == directories.end())
            directories.push_back(directory);
        start = end + 1;
    }
    return directories;
}
std::string ParseGitVersion(const std::string& output) {
    // "git version 2.54.0" possibly followed by platform suffixes.
    auto at = output.find("git version ");
    if (at == std::string::npos) return "";
    at += 12;
    std::string version;
    while (at < output.size() && (std::isdigit(static_cast<unsigned char>(output[at])) || output[at] == '.'))
        version += output[at++];
    while (!version.empty() && version.back() == '.') version.pop_back();
    return version;
}
int CompareGitVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& text) {
        std::vector<long> parts;
        size_t at = 0;
        while (at < text.size()) {
            char* end = nullptr;
            parts.push_back(std::strtol(text.c_str() + at, &end, 10));
            at = end - text.c_str();
            if (at < text.size() && text[at] == '.') ++at; else break;
        }
        return parts;
    };
    auto pa = parse(a), pb = parse(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}
GitInstallation ProbeGitExecutable(const std::string& path, const std::string& origin) {
    GitInstallation installation{path, {}, origin};
    if (!Executable(path)) return installation;
    ProcessOptions options;
    options.executable = path;
    options.timeout = std::chrono::milliseconds(5000);
    options.environment = {"GIT_TERMINAL_PROMPT=0", "LC_ALL=C"};
    auto result = RunProcess({"git", "--version"}, options);
    if (result.ok()) installation.version = ParseGitVersion(result.output);
    return installation;
}
std::vector<GitInstallation> DiscoverGitInstallations(const char* pathEnvironment) {
    std::vector<GitInstallation> found;
    std::vector<std::string> seen;
    for (const auto& directory : GitSearchDirectories(pathEnvironment)) {
        auto candidate = directory + "/git";
        if (!Executable(candidate)) continue;
        auto real = RealPath(candidate);
        if (std::find(seen.begin(), seen.end(), real) != seen.end()) continue;
        seen.push_back(real);
        std::string origin = "path";
        if (directory == "/boot/system/bin") origin = "system";
        else if (directory.find("non-packaged") != std::string::npos) origin = "non-packaged";
        else if (directory.rfind(Home(), 0) == 0) origin = "home";
        auto installation = ProbeGitExecutable(candidate, origin);
        if (installation.usable()) found.push_back(std::move(installation));
    }
    return found;
}
}
