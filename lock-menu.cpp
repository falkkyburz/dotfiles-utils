#include "imgui_app.h"

#include <vector>

int main()
{
    return ui::runButtonMenu(
        "lock-menu", 240, 280,
        {
            {"Lock", [] { ui::runDetachedShell("$HOME/.local/bin/lock-session.sh"); }},
            {"Suspend", [] { ui::runDetachedShell("systemctl suspend"); }},
            {"Reboot", [] { ui::runDetachedShell("hyprshutdown --top-label 'Rebooting...' --post-cmd 'systemctl reboot'"); }},
            {"Shutdown", [] { ui::runDetachedShell("hyprshutdown --top-label 'Shutting down...' --post-cmd 'systemctl poweroff'"); }},
        });
}
