#include "echo/apps/framework/supervisor.hpp"
#include "echo/apps/framework/ipc.hpp"
#include "echo/log.hpp"

#include <algorithm>

namespace echo::apps {

Supervisor::~Supervisor() { shutdown(); }

void Supervisor::add(const AppSpec& spec) {
    auto m  = std::make_unique<Managed>();
    m->spec = spec;
    apps_.push_back(std::move(m));
}

Supervisor::Managed* Supervisor::find(const std::string& id) {
    for (auto& m : apps_)
        if (m->spec.id == id) return m.get();
    return nullptr;
}

const Supervisor::Managed* Supervisor::find(const std::string& id) const {
    for (auto& m : apps_)
        if (m->spec.id == id) return m.get();
    return nullptr;
}

Status Supervisor::start(const std::string& id) {
    Managed* m = find(id);
    if (!m) return Status::Unavailable;
    if (m->proc && m->proc->running()) return Status::Ok;  // already up

    SpawnOptions opts;
    opts.executable = m->spec.executable;
    opts.args       = m->spec.args;
    opts.args.push_back("--serve");  // supervised apps always serve over stdio

    auto spawned = ChildProcess::spawn(opts);
    if (!spawned) {
        char buf[192];
        (void)std::snprintf(buf, sizeof(buf), "failed to spawn app '%s' (%s)",
                            id.c_str(), m->spec.executable.c_str());
        log_error("supervisor", buf);
        return spawned.status();
    }
    m->proc = std::make_unique<ChildProcess>(std::move(spawned.value()));
    char buf[128];
    (void)std::snprintf(buf, sizeof(buf), "app '%s' started (isolated process)", id.c_str());
    log_info("supervisor", buf);
    return Status::Ok;
}

void Supervisor::stop(const std::string& id) {
    Managed* m = find(id);
    if (!m || !m->proc) return;
    m->proc->terminate();
    m->last_exit_code = m->proc->exit_code();
    m->proc.reset();
    char buf[96];
    (void)std::snprintf(buf, sizeof(buf), "app '%s' stopped", id.c_str());
    log_info("supervisor", buf);
}

Status Supervisor::restart(const std::string& id) {
    stop(id);
    return start(id);
}

int Supervisor::start_all() {
    int up = 0;
    for (auto& m : apps_)
        if (start(m->spec.id) == Status::Ok) ++up;
    return up;
}

int Supervisor::reap() {
    int restarted = 0;
    for (auto& m : apps_) {
        if (!m->proc || m->gave_up) continue;
        if (m->proc->running()) continue;

        // The app exited on its own — a crash from the supervisor's point of
        // view. Record it; the core loop never noticed (constraint #2).
        m->last_exit_code = m->proc->exit_code();
        m->proc.reset();

        char buf[192];
        (void)std::snprintf(buf, sizeof(buf), "app '%s' exited (code=%d)",
                            m->spec.id.c_str(),
                            m->last_exit_code ? *m->last_exit_code : -1);
        log_warn("supervisor", buf);

        if (!m->spec.auto_restart) continue;
        if (m->restarts >= m->spec.max_restarts) {
            m->gave_up = true;
            (void)std::snprintf(buf, sizeof(buf),
                                "app '%s' exceeded %d restarts — parking it",
                                m->spec.id.c_str(), m->spec.max_restarts);
            log_error("supervisor", buf);
            continue;
        }
        ++m->restarts;
        if (start(m->spec.id) == Status::Ok) ++restarted;
    }
    return restarted;
}

std::optional<AppResponse> Supervisor::send_command(const std::string& id,
                                                    const VoiceCommand& command) {
    Managed* m = find(id);
    if (!m || !m->proc || !m->proc->running()) return std::nullopt;

    if (!m->proc->write_line(ipc::encode(ipc::to_message(command)))) return std::nullopt;
    auto line = m->proc->read_line();
    if (!line) return std::nullopt;
    auto msg = ipc::decode(*line);
    if (!msg || msg->kind != ipc::MessageKind::Rsp) return std::nullopt;
    return ipc::to_response(*msg);
}

AppStatus Supervisor::status(const std::string& id) const {
    AppStatus s;
    const Managed* m = find(id);
    if (!m) return s;
    s.id             = m->spec.id;
    s.running        = m->proc != nullptr;  // best-effort snapshot
    s.restarts       = m->restarts;
    s.gave_up        = m->gave_up;
    s.last_exit_code = m->last_exit_code;
    return s;
}

std::vector<AppStatus> Supervisor::statuses() const {
    std::vector<AppStatus> out;
    out.reserve(apps_.size());
    for (auto& m : apps_) out.push_back(status(m->spec.id));
    return out;
}

void Supervisor::shutdown() {
    for (auto& m : apps_) {
        if (m->proc) {
            m->proc->terminate();
            m->last_exit_code = m->proc->exit_code();
            m->proc.reset();
        }
    }
}

}  // namespace echo::apps
