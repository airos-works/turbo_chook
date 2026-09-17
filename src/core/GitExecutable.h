#pragma once
#include <string>
#include <vector>

namespace tc {
// One Git installation. Several can coexist on Haiku: the packaged system Git,
// non-packaged builds under /boot/system or /boot/home/config, and custom paths.
struct GitInstallation {
    std::string path;      // absolute path to the git executable
    std::string version;   // "2.54.0" or empty when the executable did not answer
    std::string origin;    // "system", "non-packaged", "home", "path", "custom"
    bool usable() const { return !version.empty(); }
    std::string label() const;
};
// Directories searched for git executables, in priority order. The PATH
// environment is appended so unusual installs are found too.
std::vector<std::string> GitSearchDirectories(const char* pathEnvironment);
// Runs each candidate with --version. Duplicate real paths are collapsed.
std::vector<GitInstallation> DiscoverGitInstallations(const char* pathEnvironment = nullptr);
// Describes a single executable; version stays empty when it does not run.
GitInstallation ProbeGitExecutable(const std::string& path, const std::string& origin = "custom");
std::string ParseGitVersion(const std::string& versionOutput);
// Numeric ordering: "2.54.0" > "2.45.2". Unknown strings compare lowest.
int CompareGitVersions(const std::string& a, const std::string& b);
}
