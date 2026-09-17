#pragma once
#include "core/Process.h"
#include <atomic>
#include <string>
#include <vector>

namespace tc {
// Pull requests through the GitHub REST API. Requests run through the
// system curl so the app has no TLS dependency of its own; the personal
// access token comes from the preferences.
struct PullRequest {
    int number = 0;
    std::string title, body, state, author, url, createdAt, updatedAt, mergedAt;
    std::string headRef, headSha, headRepository, headCloneURL, baseRef;
    bool draft = false, merged = false;
    int comments = 0, commits = 0, additions = 0, deletions = 0, changedFiles = 0;
    std::string mergeable;   // "", "mergeable", "conflicting"
    std::string statusLine() const;
};
struct PullRequestComment { std::string author, body, createdAt, path; int line = 0; };
struct PullRequestReview { std::string author, state, body, submittedAt; };
struct HttpResponse { int status = 0; std::string body, error; bool ok() const { return error.empty() && status >= 200 && status < 300; } };
struct GitHubOptions {
    std::string token;
    std::string apiBase = "https://api.github.com";
    std::string curlExecutable = "curl";
    std::chrono::milliseconds timeout{30000};
};
class GitHubClient {
public:
    GitHubClient(GitHubOptions options, std::string repository /* owner/name */);
    const std::string& Repository() const { return fRepository; }
    // Parsed API results carry an error string when the request failed.
    template <typename T> struct Result { T value{}; std::string error; bool ok() const { return error.empty(); } };
    Result<std::vector<PullRequest>> ListPullRequests(const std::string& state /* open, closed, all */, Cancel cancel = nullptr) const;
    Result<PullRequest> GetPullRequest(int number, Cancel cancel = nullptr) const;
    Result<PullRequest> CreatePullRequest(const std::string& title, const std::string& body, const std::string& head,
        const std::string& base, bool draft, Cancel cancel = nullptr) const;
    Result<std::string> MergePullRequest(int number, const std::string& method /* merge, squash, rebase */, Cancel cancel = nullptr) const;
    Result<std::string> ClosePullRequest(int number, bool reopen, Cancel cancel = nullptr) const;
    Result<std::vector<PullRequestComment>> Comments(int number, Cancel cancel = nullptr) const;
    Result<std::vector<PullRequestReview>> Reviews(int number, Cancel cancel = nullptr) const;
    Result<std::string> AddComment(int number, const std::string& body, Cancel cancel = nullptr) const;
    Result<std::string> Viewer(Cancel cancel = nullptr) const;   // the login for the configured token
    Result<std::string> DefaultBranch(Cancel cancel = nullptr) const;
    HttpResponse Request(const std::string& method, const std::string& path, const std::string& body = {}, Cancel cancel = nullptr) const;
private:
    GitHubOptions fOptions;
    std::string fRepository;
};
// Exposed for tests: parse API JSON without the network.
std::vector<PullRequest> ParsePullRequests(const std::string& json);
PullRequest ParsePullRequest(const std::string& json);
std::string GitHubErrorMessage(const HttpResponse& response);
}
