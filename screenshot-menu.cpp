#include "imgui_app.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string regionCommand(bool menuDelay, bool includeCursor)
{
    return std::string(menuDelay ? "sleep 0.18; " : "") +
           "screenfreeze" + (includeCursor ? " --cursor" : "") +
           " & freeze_pid=$!; trap 'kill \"$freeze_pid\" 2>/dev/null' EXIT; sleep 0.15; "
           "geom=\"$(slurp)\" || exit 0; tmp=\"$(mktemp --suffix=.png)\" || exit 1; "
           "grim" + (includeCursor ? " -c" : "") +
           " -g \"$geom\" \"$tmp\" || exit 1; kill \"$freeze_pid\" 2>/dev/null; "
           "wait \"$freeze_pid\" 2>/dev/null; trap 'rm -f \"$tmp\"' EXIT; satty --filename \"$tmp\"";
}

std::string readFocusedWindow()
{
    return ui::readCommandOutput("hyprctl activewindow -j | jq -r '.address // empty'");
}

void runScreenshotCommand(const std::string &command)
{
    if (!ui::runDetachedShell(command)) {
        std::fputs("screenshot-menu: failed to start screenshot command\n", stderr);
    }
}

void printUsage()
{
    std::fputs("Usage: screenshot-menu [--region]\n\n"
               "Options:\n"
               "  -r, --region  Capture a region without opening the GUI.\n"
               "  -c, --cursor  Include the cursor when used with --region.\n"
               "  -h, --help    Show this help text.\n",
               stdout);
}

} // namespace

int main(int argc, char *argv[])
{
    bool region = false;
    bool includeCursor = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        if (arg == "-r" || arg == "--region") {
            region = true;
            continue;
        }
        if (arg == "-c" || arg == "--cursor") {
            includeCursor = true;
            continue;
        }

        std::fprintf(stderr, "screenshot-menu: unknown option: %s\n", argv[i]);
        return 2;
    }

    if (includeCursor && !region) {
        std::fputs("screenshot-menu: --cursor requires --region\n", stderr);
        return 2;
    }

    if (region) {
        return ui::runDetachedShell(regionCommand(false, includeCursor)) ? 0 : 1;
    }

    const std::string focusedWindow = readFocusedWindow();

    return ui::runButtonMenu(
        "screenshot-menu", 240, 220,
        {
            {"Region", [] { runScreenshotCommand(regionCommand(true, false)); }},
            {"Focused Window",
             [focusedWindow] {
                 if (focusedWindow.empty() || focusedWindow == "null") {
                     std::fputs("screenshot-menu: no focused window address available\n", stderr);
                     return;
                 }
                 runScreenshotCommand(
                     "sleep 0.18; geom=\"$(hyprctl clients -j | jq -r --arg addr " + ui::shellQuote(focusedWindow) +
                     " '.[] | select(.address == $addr) | \"\\(.at[0]),\\(.at[1]) \\(.size[0])x\\(.size[1])\"')\"; "
                     "[ -n \"$geom\" ] && [ \"$geom\" != \"null\" ] || exit 1; grim -g \"$geom\" - | satty --filename -");
             }},
            {"Full Screen",
             [] {
                 runScreenshotCommand(
                     "sleep 0.18; mon=\"$(hyprctl monitors -j | jq -r '.[] | select(.focused == true) | .name' | head -n1)\"; "
                     "[ -n \"$mon\" ] && [ \"$mon\" != \"null\" ] || exit 1; grim -o \"$mon\" - | satty --filename -");
             }},
        });
}
