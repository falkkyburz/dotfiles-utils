#include "imgui_app.h"

#include <cstdio>
#include <string>

namespace {

struct WorkspaceLayout {
    std::string id;
    std::string layout = "dwindle";
};

WorkspaceLayout readCurrentWorkspaceLayout()
{
    const std::string output = ui::readCommandOutput("hyprctl activeworkspace -j 2>/dev/null | jq -r '.id, .tiledLayout'");
    const auto separator = output.find('\n');
    if (separator != std::string::npos) {
        const std::string id = ui::trim(output.substr(0, separator));
        const std::string layout = ui::trim(output.substr(separator + 1));
        const std::size_t digitsStart = id.starts_with('-') ? 1 : 0;
        const bool validId = digitsStart < id.size() && id.find_first_not_of("0123456789", digitsStart) == std::string::npos;
        if (validId && !layout.empty()) {
            return {id, layout};
        }
    }
    if (!output.empty()) {
        std::fputs("layout-menu: failed to parse current Hyprland workspace layout\n", stderr);
    }
    return {};
}

void setLayout(const std::string& workspaceId, const char *layout)
{
    if (workspaceId.empty()) {
        std::fputs("layout-menu: failed to identify the current Hyprland workspace\n", stderr);
        return;
    }

    const std::string expression = "hl.workspace_rule({ workspace = \"" + workspaceId + "\", layout = \"" + layout + "\" })";
    const std::string command = "hyprctl eval " + ui::shellQuote(expression) + " >/dev/null 2>&1";
    if (!ui::commandSucceeded(command)) {
        std::fprintf(stderr, "layout-menu: failed to set layout %s for Hyprland workspace %s\n", layout, workspaceId.c_str());
    }
}

} // namespace

int main()
{
    const auto [workspaceId, currentLayout] = readCurrentWorkspaceLayout();
    int selected = 0;
    if (currentLayout == "master") {
        selected = 1;
    } else if (currentLayout == "scrolling") {
        selected = 2;
    } else if (currentLayout == "monocle") {
        selected = 3;
    }

    return ui::runButtonMenu(
        "layout-menu", 240, 280,
        {
            {"Dwindle", [workspaceId] { setLayout(workspaceId, "dwindle"); }, currentLayout == "dwindle"},
            {"Master", [workspaceId] { setLayout(workspaceId, "master"); }, currentLayout == "master"},
            {"Scrolling", [workspaceId] { setLayout(workspaceId, "scrolling"); }, currentLayout == "scrolling"},
            {"Monocle", [workspaceId] { setLayout(workspaceId, "monocle"); }, currentLayout == "monocle"},
        },
        selected);
}
