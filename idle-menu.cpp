#include "imgui_app.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::filesystem::path pidPath()
{
    const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    return std::filesystem::path(runtimeDir != nullptr ? runtimeDir : "/tmp") / "qt-idle-inhibit.pid";
}

long readPid()
{
    std::ifstream file(pidPath());
    long pid = 0;
    file >> pid;
    return file ? pid : 0;
}

bool writePid(int pid)
{
    std::ofstream file(pidPath(), std::ios::trunc);
    if (!file) {
        return false;
    }
    file << pid << '\n';
    return static_cast<bool>(file);
}

bool processExists(long pid)
{
    return pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
}

bool isInhibited()
{
    const long pid = readPid();
    if (processExists(pid)) {
        return true;
    }
    std::filesystem::remove(pidPath());
    return false;
}

void enableInhibit()
{
    if (isInhibited()) {
        return;
    }

    int pid = 0;
    const bool started = ui::startDetachedProcess(
        "systemd-inhibit",
        {"--what=idle", "--who=idle-menu", "--why=User requested idle inhibit", "sleep", "infinity"}, &pid);
    if (!started) {
        std::fputs("idle-menu: failed to start idle inhibitor\n", stderr);
    } else if (!writePid(pid)) {
        std::cerr << "idle-menu: failed to write idle inhibitor pid " << pid << '\n';
    }
}

void disableInhibit()
{
    const long pid = readPid();
    if (pid > 0) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    }
    std::filesystem::remove(pidPath());
}

} // namespace

int main()
{
    const bool inhibited = isInhibited();
    return ui::runButtonMenu(
        "idle-menu", 240, 160,
        {
            {"Keep Awake", enableInhibit, inhibited},
            {"Allow Idle", disableInhibit, !inhibited},
        },
        inhibited ? 0 : 1);
}
