#include "echo/apps/framework/process.hpp"
#include "echo/log.hpp"

#include <cstring>
#include <utility>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <csignal>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace echo::apps {

// --- helpers shared by both platforms ---------------------------------------
namespace {

// Extract the next complete line from a growing buffer. Returns true and fills
// `line` (without the newline) if one is available, trimming a trailing '\r'.
bool take_line(std::string& buf, std::string& line) {
    auto nl = buf.find('\n');
    if (nl == std::string::npos) return false;
    line = buf.substr(0, nl);
    buf.erase(0, nl + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

}  // namespace

// ============================================================================
#if defined(_WIN32)
// ============================================================================

namespace {

std::string quote_arg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string q = "\"";
    for (char c : a) {
        if (c == '"') q += '\\';
        q += c;
    }
    q += '"';
    return q;
}

}  // namespace

Result<ChildProcess> ChildProcess::spawn(const SpawnOptions& opts) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE in_read = nullptr, in_write = nullptr;    // child stdin  (parent writes in_write)
    HANDLE out_read = nullptr, out_write = nullptr;  // child stdout (parent reads out_read)

    if (!CreatePipe(&in_read, &in_write, &sa, 0) ||
        !CreatePipe(&out_read, &out_write, &sa, 0)) {
        log_error("process", "CreatePipe failed");
        return Result<ChildProcess>::fail(Status::HardwareError);
    }
    // The parent's ends must NOT be inherited by the child.
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

    std::string cmdline = quote_arg(opts.executable);
    for (const auto& a : opts.args) { cmdline += ' '; cmdline += quote_arg(a); }

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = in_read;
    si.hStdOutput = out_write;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);  // child logs flow to our stderr

    PROCESS_INFORMATION pi{};
    std::string mutable_cmd = cmdline;  // CreateProcessA may modify the buffer
    BOOL ok = CreateProcessA(
        /*app*/ nullptr, mutable_cmd.data(), nullptr, nullptr,
        /*inherit*/ TRUE, /*flags*/ 0, nullptr, nullptr, &si, &pi);

    // The child owns its ends now; close ours regardless of success.
    CloseHandle(in_read);
    CloseHandle(out_write);

    if (!ok) {
        CloseHandle(in_write);
        CloseHandle(out_read);
        log_error("process", "CreateProcess failed");
        return Result<ChildProcess>::fail(Status::HardwareError);
    }
    CloseHandle(pi.hThread);

    ChildProcess cp;
    cp.proc_handle_ = pi.hProcess;
    cp.stdin_write_ = in_write;
    cp.stdout_read_ = out_read;
    return Result<ChildProcess>::ok(std::move(cp));
}

bool ChildProcess::write_line(const std::string& line) {
    if (!stdin_write_) return false;
    std::string payload = line;
    payload += '\n';
    DWORD written = 0;
    return WriteFile(stdin_write_, payload.data(),
                     static_cast<DWORD>(payload.size()), &written, nullptr) &&
           written == payload.size();
}

std::optional<std::string> ChildProcess::read_line() {
    std::string line;
    if (take_line(read_buf_, line)) return line;
    char chunk[512];
    while (stdout_read_) {
        DWORD got = 0;
        if (!ReadFile(stdout_read_, chunk, sizeof(chunk), &got, nullptr) || got == 0)
            return std::nullopt;  // pipe closed
        read_buf_.append(chunk, got);
        if (take_line(read_buf_, line)) return line;
    }
    return std::nullopt;
}

bool ChildProcess::running() {
    if (!proc_handle_) return false;
    DWORD wait = WaitForSingleObject(proc_handle_, 0);
    if (wait == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    if (GetExitCodeProcess(proc_handle_, &code)) exit_code_ = static_cast<int>(code);
    return false;
}

void ChildProcess::terminate() {
    if (proc_handle_ && running()) TerminateProcess(proc_handle_, 1);
}

void ChildProcess::close_all() noexcept {
    if (stdin_write_) { CloseHandle(stdin_write_); stdin_write_ = nullptr; }
    if (stdout_read_) { CloseHandle(stdout_read_); stdout_read_ = nullptr; }
    if (proc_handle_) { CloseHandle(proc_handle_); proc_handle_ = nullptr; }
}

// ============================================================================
#else  // POSIX
// ============================================================================

Result<ChildProcess> ChildProcess::spawn(const SpawnOptions& opts) {
    int in_pipe[2], out_pipe[2];  // in: parent->child, out: child->parent
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        log_error("process", "pipe() failed");
        return Result<ChildProcess>::fail(Status::HardwareError);
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_error("process", "fork() failed");
        return Result<ChildProcess>::fail(Status::HardwareError);
    }

    if (pid == 0) {
        // Child: wire pipe ends to stdio and exec the app binary.
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(opts.executable.c_str()));
        for (const auto& a : opts.args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(opts.executable.c_str(), argv.data());
        _exit(127);  // exec failed
    }

    // Parent: keep the write-to-child and read-from-child ends.
    close(in_pipe[0]);
    close(out_pipe[1]);

    ChildProcess cp;
    cp.pid_         = pid;
    cp.stdin_write_ = in_pipe[1];
    cp.stdout_read_ = out_pipe[0];
    return Result<ChildProcess>::ok(std::move(cp));
}

bool ChildProcess::write_line(const std::string& line) {
    if (stdin_write_ < 0) return false;
    std::string payload = line;
    payload += '\n';
    ssize_t n = ::write(stdin_write_, payload.data(), payload.size());
    return n == static_cast<ssize_t>(payload.size());
}

std::optional<std::string> ChildProcess::read_line() {
    std::string line;
    if (take_line(read_buf_, line)) return line;
    char chunk[512];
    while (stdout_read_ >= 0) {
        ssize_t got = ::read(stdout_read_, chunk, sizeof(chunk));
        if (got <= 0) return std::nullopt;  // EOF/error
        read_buf_.append(chunk, static_cast<std::size_t>(got));
        if (take_line(read_buf_, line)) return line;
    }
    return std::nullopt;
}

bool ChildProcess::running() {
    if (pid_ < 0) return false;
    int status = 0;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r == 0) return true;
    if (r == pid_) {
        if (WIFEXITED(status))        exit_code_ = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exit_code_ = 128 + WTERMSIG(status);
        pid_ = -1;
    }
    return false;
}

void ChildProcess::terminate() {
    if (pid_ >= 0) { ::kill(pid_, SIGKILL); running(); }
}

void ChildProcess::close_all() noexcept {
    if (stdin_write_ >= 0) { ::close(stdin_write_); stdin_write_ = -1; }
    if (stdout_read_ >= 0) { ::close(stdout_read_); stdout_read_ = -1; }
}

#endif  // platform

// --- portable pieces of the lifecycle ---------------------------------------

ChildProcess::~ChildProcess() {
    terminate();
    close_all();
}

ChildProcess::ChildProcess(ChildProcess&& o) noexcept { *this = std::move(o); }

ChildProcess& ChildProcess::operator=(ChildProcess&& o) noexcept {
    if (this == &o) return *this;
    terminate();
    close_all();
#if defined(_WIN32)
    proc_handle_   = o.proc_handle_;   o.proc_handle_ = nullptr;
    stdin_write_   = o.stdin_write_;   o.stdin_write_ = nullptr;
    stdout_read_   = o.stdout_read_;   o.stdout_read_ = nullptr;
#else
    pid_           = o.pid_;           o.pid_ = -1;
    stdin_write_   = o.stdin_write_;   o.stdin_write_ = -1;
    stdout_read_   = o.stdout_read_;   o.stdout_read_ = -1;
#endif
    read_buf_  = std::move(o.read_buf_);
    exit_code_ = o.exit_code_;
    return *this;
}

}  // namespace echo::apps
