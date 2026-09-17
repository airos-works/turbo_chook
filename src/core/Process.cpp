#include "core/Process.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace tc {
namespace {
bool MakePipe(int (&fds)[2]) {
    if (pipe(fds) != 0) return false;
    for (int fd : fds) fcntl(fd, F_SETFD, FD_CLOEXEC);
    return true;
}
void Nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }
std::string Executable(const std::string& name) {
    if (name.find('/') != std::string::npos) return name;
    const char* path = getenv("PATH");
    std::string paths = path ? path : "/bin:/usr/bin:/boot/system/bin";
    size_t start = 0;
    while (start <= paths.size()) {
        size_t end = paths.find(':', start);
        if (end == std::string::npos) end = paths.size();
        auto candidate = paths.substr(start, end - start) + "/" + name;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
        start = end + 1;
    }
    return name;
}
}
std::string ProcessResult::diagnostic() const {
    if (cancelled) return "Operation cancelled.";
    if (timedOut) return "Operation timed out.";
    if (truncated) return "Output exceeded the display limit. Narrow the selection and try again.";
    if (!error.empty()) return error;
    if (!output.empty()) return output;
    return "Process exited with status " + std::to_string(exitCode) + ".";
}
ProcessResult RunProcess(const std::vector<std::string>& args, const ProcessOptions& options) {
    ProcessResult result;
    if (args.empty()) { result.error = "No command specified."; return result; }
    std::string executable = options.executable.empty() ? Executable(args[0]) : options.executable;
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> env;
    for (char** entry = environ; *entry; ++entry) env.emplace_back(*entry);
    for (const auto& entry : options.environment) {
        auto prefix = entry.substr(0, entry.find('=') + 1);
        env.erase(std::remove_if(env.begin(), env.end(), [&](const auto& e) {
            return e.compare(0, prefix.size(), prefix) == 0;
        }), env.end());
        env.push_back(entry);
    }
    std::vector<char*> envp;
    for (auto& entry : env) envp.push_back(entry.data());
    envp.push_back(nullptr);
    int out[2]{-1,-1}, err[2]{-1,-1}, in[2]{-1,-1};
    if (!MakePipe(out) || !MakePipe(err) || !MakePipe(in)) {
        result.error = strerror(errno);
        for (int fd : {out[0], out[1], err[0], err[1], in[0], in[1]}) if (fd >= 0) close(fd);
        return result;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        dup2(err[1], STDERR_FILENO);
        for (int fd : {out[0], out[1], err[0], err[1], in[0], in[1]}) close(fd);
        if (!options.directory.empty() && chdir(options.directory.c_str()) != 0) _exit(126);
        // Only async-signal-safe operations between fork and exec in this multithreaded app.
        execve(executable.c_str(), argv.data(), envp.data());
        _exit(127);
    }
    close(out[1]); close(err[1]); close(in[0]);
    if (pid < 0) {
        result.error = strerror(errno); close(out[0]); close(err[0]); close(in[1]); return result;
    }
    setpgid(pid, pid);
    Nonblock(out[0]); Nonblock(err[0]); Nonblock(in[1]);
    size_t written = 0;
    if (options.input.empty()) { close(in[1]); in[1] = -1; }
    auto start = std::chrono::steady_clock::now();
    bool reaped = false, killed = false;
    int status = 0;
    while (!reaped || out[0] >= 0 || err[0] >= 0) {
        if (!killed) {
            result.cancelled = options.cancel && options.cancel->load();
            result.timedOut = std::chrono::steady_clock::now() - start > options.timeout;
            if (result.cancelled || result.timedOut || result.truncated) {
                kill(-pid, SIGKILL); kill(pid, SIGKILL); killed = true;
            }
        }
        pollfd fds[] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}, {in[1], POLLOUT, 0}};
        poll(fds, 3, 25);
        auto drain = [&](int& fd, std::string& target) {
            if (fd < 0) return;
            char buffer[16384];
            // Bound each drain so continuous output cannot starve timeout/cancellation.
            for (int batch = 0; batch < 16; ++batch) {
                ssize_t n = read(fd, buffer, sizeof(buffer));
                if (n > 0) {
                    size_t available = options.outputLimit > result.output.size() + result.error.size()
                        ? options.outputLimit - result.output.size() - result.error.size() : 0;
                    target.append(buffer, std::min(available, static_cast<size_t>(n)));
                    if (static_cast<size_t>(n) > available) result.truncated = true;
                } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                    close(fd); fd = -1; break;
                } else break;
            }
        };
        drain(out[0], result.output); drain(err[0], result.error);
        if (in[1] >= 0 && fds[2].revents) {
            // The application ignores SIGPIPE; a closed child's stdin becomes EPIPE.
            ssize_t n = write(in[1], options.input.data() + written, options.input.size() - written);
            if (n > 0) written += static_cast<size_t>(n);
            if (written == options.input.size() || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                close(in[1]); in[1] = -1;
            }
        }
        if (!reaped) reaped = waitpid(pid, &status, WNOHANG) == pid;
        if (killed && reaped) {
            // Descendants may have detached and kept pipes open. Never wait forever.
            if (out[0] >= 0) { close(out[0]); out[0] = -1; }
            if (err[0] >= 0) { close(err[0]); err[0] = -1; }
        }
    }
    if (in[1] >= 0) close(in[1]);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (result.exitCode == 127 && result.error.empty()) result.error = "Cannot run " + args[0] + ". Check that it is installed.";
    if (result.exitCode == 126 && result.error.empty()) result.error = "Cannot access working directory.";
    return result;
}
}
