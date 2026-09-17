#include "core/GitHub.h"
#include "nlohmann/json.hpp"
#include "core/Git.h"

namespace tc {
using json = nlohmann::json;
namespace {
std::string Text(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}
int Number(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int>() : 0;
}
bool Flag(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}
PullRequest FromJson(const json& j) {
    PullRequest pr;
    pr.number = Number(j, "number");
    pr.title = Text(j, "title");
    pr.body = Text(j, "body");
    pr.state = Text(j, "state");
    pr.url = Text(j, "html_url");
    pr.createdAt = Text(j, "created_at");
    pr.updatedAt = Text(j, "updated_at");
    pr.mergedAt = Text(j, "merged_at");
    pr.draft = Flag(j, "draft");
    pr.merged = Flag(j, "merged") || !pr.mergedAt.empty();
    pr.comments = Number(j, "comments");
    pr.commits = Number(j, "commits");
    pr.additions = Number(j, "additions");
    pr.deletions = Number(j, "deletions");
    pr.changedFiles = Number(j, "changed_files");
    if (j.contains("mergeable") && j["mergeable"].is_boolean()) pr.mergeable = j["mergeable"].get<bool>() ? "mergeable" : "conflicting";
    if (j.contains("user") && j["user"].is_object()) pr.author = Text(j["user"], "login");
    if (j.contains("head") && j["head"].is_object()) {
        const auto& head = j["head"];
        pr.headRef = Text(head, "ref"); pr.headSha = Text(head, "sha");
        if (head.contains("repo") && head["repo"].is_object()) {
            pr.headRepository = Text(head["repo"], "full_name");
            pr.headCloneURL = Text(head["repo"], "clone_url");
        }
    }
    if (j.contains("base") && j["base"].is_object()) pr.baseRef = Text(j["base"], "ref");
    return pr;
}
}
std::string PullRequest::statusLine() const {
    if (merged) return "merged";
    if (state == "closed") return "closed";
    if (draft) return "draft";
    return "open";
}
std::vector<PullRequest> ParsePullRequests(const std::string& text) {
    std::vector<PullRequest> result;
    auto document = json::parse(text, nullptr, false);
    if (!document.is_array()) return result;
    for (const auto& entry : document) if (entry.is_object()) result.push_back(FromJson(entry));
    return result;
}
PullRequest ParsePullRequest(const std::string& text) {
    auto document = json::parse(text, nullptr, false);
    return document.is_object() ? FromJson(document) : PullRequest{};
}
std::string GitHubErrorMessage(const HttpResponse& response) {
    if (!response.error.empty()) return response.error;
    auto document = json::parse(response.body, nullptr, false);
    std::string message = document.is_object() ? Text(document, "message") : "";
    if (document.is_object() && document.contains("errors") && document["errors"].is_array()) {
        for (const auto& e : document["errors"]) {
            if (e.is_object() && !Text(e, "message").empty()) message += "\n" + Text(e, "message");
            else if (e.is_string()) message += "\n" + e.get<std::string>();
        }
    }
    if (message.empty()) message = "GitHub returned HTTP " + std::to_string(response.status) + ".";
    if (response.status == 401) message += "\nCheck the GitHub token in Preferences.";
    if (response.status == 404) message += "\nThe repository may be private; a token with repo access is required.";
    return message;
}
GitHubClient::GitHubClient(GitHubOptions options, std::string repository)
    : fOptions(std::move(options)), fRepository(std::move(repository)) {}
HttpResponse GitHubClient::Request(const std::string& method, const std::string& path, const std::string& body, Cancel cancel) const {
    HttpResponse response;
    std::vector<std::string> args{fOptions.curlExecutable, "--silent", "--show-error", "--location",
        "--max-time", std::to_string(fOptions.timeout.count() / 1000), "--request", method,
        "--header", "Accept: application/vnd.github+json", "--header", "X-GitHub-Api-Version: 2022-11-28",
        "--header", "User-Agent: TurboChook", "--write-out", "\n%{http_code}"};
    if (!fOptions.token.empty()) { args.push_back("--header"); args.push_back("Authorization: Bearer " + fOptions.token); }
    if (!body.empty()) { args.push_back("--header"); args.push_back("Content-Type: application/json"); args.push_back("--data-binary"); args.push_back("@-"); }
    args.push_back(fOptions.apiBase + path);
    ProcessOptions options;
    options.input = body; options.cancel = cancel; options.timeout = fOptions.timeout + std::chrono::milliseconds(5000);
    options.environment = {"LC_ALL=C"};
    auto result = RunProcess(args, options);
    if (result.cancelled) { response.error = "Cancelled."; return response; }
    if (result.exitCode == 127) { response.error = "curl is not installed; pull requests need the curl command."; return response; }
    if (result.exitCode != 0 && result.output.empty()) { response.error = result.diagnostic(); return response; }
    auto newline = result.output.rfind('\n');
    if (newline == std::string::npos) { response.error = "Unexpected response from GitHub."; return response; }
    response.status = std::atoi(result.output.c_str() + newline + 1);
    response.body = result.output.substr(0, newline);
    if (response.status == 0) response.error = result.error.empty() ? "Could not reach GitHub." : Trim(result.error);
    return response;
}
GitHubClient::Result<std::vector<PullRequest>> GitHubClient::ListPullRequests(const std::string& state, Cancel cancel) const {
    Result<std::vector<PullRequest>> result;
    auto response = Request("GET", "/repos/" + fRepository + "/pulls?state=" + state + "&per_page=100&sort=updated&direction=desc", {}, cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    result.value = ParsePullRequests(response.body);
    return result;
}
GitHubClient::Result<PullRequest> GitHubClient::GetPullRequest(int number, Cancel cancel) const {
    Result<PullRequest> result;
    auto response = Request("GET", "/repos/" + fRepository + "/pulls/" + std::to_string(number), {}, cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    result.value = ParsePullRequest(response.body);
    return result;
}
GitHubClient::Result<PullRequest> GitHubClient::CreatePullRequest(const std::string& title, const std::string& body,
    const std::string& head, const std::string& base, bool draft, Cancel cancel) const {
    Result<PullRequest> result;
    json payload{{"title", title}, {"body", body}, {"head", head}, {"base", base}, {"draft", draft}};
    auto response = Request("POST", "/repos/" + fRepository + "/pulls", payload.dump(), cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    result.value = ParsePullRequest(response.body);
    return result;
}
GitHubClient::Result<std::string> GitHubClient::MergePullRequest(int number, const std::string& method, Cancel cancel) const {
    Result<std::string> result;
    json payload{{"merge_method", method}};
    auto response = Request("PUT", "/repos/" + fRepository + "/pulls/" + std::to_string(number) + "/merge", payload.dump(), cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    auto document = json::parse(response.body, nullptr, false);
    result.value = document.is_object() ? Text(document, "sha") : "";
    return result;
}
GitHubClient::Result<std::string> GitHubClient::ClosePullRequest(int number, bool reopen, Cancel cancel) const {
    Result<std::string> result;
    json payload{{"state", reopen ? "open" : "closed"}};
    auto response = Request("PATCH", "/repos/" + fRepository + "/pulls/" + std::to_string(number), payload.dump(), cancel);
    if (!response.ok()) result.error = GitHubErrorMessage(response);
    return result;
}
GitHubClient::Result<std::vector<PullRequestComment>> GitHubClient::Comments(int number, Cancel cancel) const {
    Result<std::vector<PullRequestComment>> result;
    auto response = Request("GET", "/repos/" + fRepository + "/issues/" + std::to_string(number) + "/comments?per_page=100", {}, cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    auto document = json::parse(response.body, nullptr, false);
    if (document.is_array()) for (const auto& c : document) {
        if (!c.is_object()) continue;
        PullRequestComment comment;
        comment.body = Text(c, "body"); comment.createdAt = Text(c, "created_at");
        if (c.contains("user") && c["user"].is_object()) comment.author = Text(c["user"], "login");
        result.value.push_back(std::move(comment));
    }
    auto review = Request("GET", "/repos/" + fRepository + "/pulls/" + std::to_string(number) + "/comments?per_page=100", {}, cancel);
    auto reviewDocument = json::parse(review.ok() ? review.body : "[]", nullptr, false);
    if (reviewDocument.is_array()) for (const auto& c : reviewDocument) {
        if (!c.is_object()) continue;
        PullRequestComment comment;
        comment.body = Text(c, "body"); comment.createdAt = Text(c, "created_at"); comment.path = Text(c, "path"); comment.line = Number(c, "line");
        if (c.contains("user") && c["user"].is_object()) comment.author = Text(c["user"], "login");
        result.value.push_back(std::move(comment));
    }
    return result;
}
GitHubClient::Result<std::vector<PullRequestReview>> GitHubClient::Reviews(int number, Cancel cancel) const {
    Result<std::vector<PullRequestReview>> result;
    auto response = Request("GET", "/repos/" + fRepository + "/pulls/" + std::to_string(number) + "/reviews?per_page=100", {}, cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    auto document = json::parse(response.body, nullptr, false);
    if (document.is_array()) for (const auto& r : document) {
        if (!r.is_object()) continue;
        PullRequestReview review;
        review.state = Text(r, "state"); review.body = Text(r, "body"); review.submittedAt = Text(r, "submitted_at");
        if (r.contains("user") && r["user"].is_object()) review.author = Text(r["user"], "login");
        result.value.push_back(std::move(review));
    }
    return result;
}
GitHubClient::Result<std::string> GitHubClient::AddComment(int number, const std::string& body, Cancel cancel) const {
    Result<std::string> result;
    json payload{{"body", body}};
    auto response = Request("POST", "/repos/" + fRepository + "/issues/" + std::to_string(number) + "/comments", payload.dump(), cancel);
    if (!response.ok()) result.error = GitHubErrorMessage(response);
    return result;
}
GitHubClient::Result<std::string> GitHubClient::Viewer(Cancel cancel) const {
    Result<std::string> result;
    auto response = Request("GET", "/user", {}, cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    auto document = json::parse(response.body, nullptr, false);
    result.value = document.is_object() ? Text(document, "login") : "";
    return result;
}
GitHubClient::Result<std::string> GitHubClient::DefaultBranch(Cancel cancel) const {
    Result<std::string> result;
    auto response = Request("GET", "/repos/" + fRepository, {}, cancel);
    if (!response.ok()) { result.error = GitHubErrorMessage(response); return result; }
    auto document = json::parse(response.body, nullptr, false);
    result.value = document.is_object() ? Text(document, "default_branch") : "";
    return result;
}
}
