// ECHO OS apps — a minimal cross-platform child-process wrapper.
//
// Isolation (constraint #2) is real, not a comment: each app is a separate OS
// process, and the supervisor talks to it over its stdio. This wrapper is the
// portable primitive under the supervisor — spawn a child with redirected
// stdin/stdout, exchange newline-delimited messages, observe liveness, and
// terminate it. If a child hangs or crashes, the parent (the core-side host) is
// entirely unaffected: it just sees the pipe close / the process exit.
//
// Implemented for Windows (CreateProcess + pipes) and POSIX (fork/exec + pipes)
// behind one interface; the rest of the layer is platform-agnostic.
#pragma once

#include "echo/result.hpp"

#include <optional>
#include <string>
#include <vector>

namespace echo::apps {

struct SpawnOptions {
    std::string              executable;  // path to the app binary
    std::vector<std::string> args;        // argv[1..] (e.g. {"--serve","--mock"})
};

// Owns a spawned child and the pipes to it. Move-only; destructor terminates the
// child if still running so a dropped handle can never leak a process.
class ChildProcess {
public:
    static Result<ChildProcess> spawn(const SpawnOptions& opts);

    ChildProcess() = default;
    ~ChildProcess();

    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;
    ChildProcess(const ChildProcess&)            = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Write one line (a '\n' is appended) to the child's stdin. False on error.
    bool write_line(const std::string& line);

    // Read one line (without the trailing '\n') from the child's stdout. Blocks
    // until a line arrives or the pipe closes; nullopt on EOF/error.
    std::optional<std::string> read_line();

    // Is the child still alive? Reaps the exit code as a side effect if it isn't.
    bool running();

    // Force-terminate the child (used on hang or shutdown). Idempotent.
    void terminate();

    // Exit code once the child has exited (nullopt while running).
    std::optional<int> exit_code() const noexcept { return exit_code_; }

private:
    void close_all() noexcept;

#if defined(_WIN32)
    void*       proc_handle_ = nullptr;  // HANDLE
    void*       stdin_write_ = nullptr;  // HANDLE (parent -> child)
    void*       stdout_read_ = nullptr;  // HANDLE (child  -> parent)
#else
    int         pid_          = -1;
    int         stdin_write_  = -1;
    int         stdout_read_  = -1;
#endif
    std::string        read_buf_;   // carry-over bytes between read_line() calls
    std::optional<int> exit_code_;
};

}  // namespace echo::apps
