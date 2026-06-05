#include "imgui_app.h"

#include <cstdio>
#include <vector>

namespace {

bool isRecording()
{
    return ui::commandSucceeded("pgrep -x wf-recorder >/dev/null 2>&1");
}

void runRecordingCommand(const char *command, bool detach = true)
{
    if (detach) {
        if (!ui::runDetachedShell(command)) {
            std::fputs("screenrecord-menu: failed to start screen recording command\n", stderr);
        }
    } else {
        ui::runBlockingShell(command);
    }
}

} // namespace

int main()
{
    if (isRecording()) {
        return ui::runButtonMenu(
            "screenrecord-menu", 240, 96,
            {
                {"Stop Recording", [] { runRecordingCommand("pkill -INT -x wf-recorder && notify-send -u low -i camera 'Recording finished'", false); }},
            });
    }

    return ui::runButtonMenu(
        "screenrecord-menu", 240, 220,
        {
            {"GIF", [] { runRecordingCommand("sleep 0.18; wf-recorder -g \"$(slurp)\" -c gif -f \"$HOME/Videos/screenrecord-$(date +%Y-%m-%dT%H-%M-%S).gif\""); }},
            {"MP4", [] { runRecordingCommand("sleep 0.18; wf-recorder -g \"$(slurp)\" -f \"$HOME/Videos/screenrecord-$(date +%Y-%m-%dT%H-%M-%S).mp4\""); }},
            {"MP4+Audio", [] { runRecordingCommand("sleep 0.18; wf-recorder -g \"$(slurp)\" -a -f \"$HOME/Videos/screenrecord-$(date +%Y-%m-%dT%H-%M-%S).mp4\""); }},
        });
}
