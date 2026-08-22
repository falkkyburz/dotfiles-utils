#include "imgui_app.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

std::string readCurrentLayout()
{
    const std::string output = ui::readCommandOutput("hyprctl getoption general:layout 2>/dev/null");
    constexpr std::string_view prefix = "str:";
    const auto position = output.find(prefix);
    if (position != std::string::npos) {
        const auto end = output.find('\n', position);
        return ui::trim(output.substr(position + prefix.size(), end - position - prefix.size()));
    }
    if (!output.empty()) {
        std::fputs("layout-menu: failed to parse current Hyprland layout\n", stderr);
    }
    return "dwindle";
}

void setLayout(const char *layout)
{
    const std::string expression = std::string("hl.config({ general = { layout = \"") + layout + "\" } })";
    const std::string command = "hyprctl eval " + ui::shellQuote(expression) + " >/dev/null 2>&1";
    if (!ui::commandSucceeded(command)) {
        std::fprintf(stderr, "layout-menu: failed to set Hyprland layout %s\n", layout);
    }
}

} // namespace

int main()
{
    const std::string currentLayout = readCurrentLayout();
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
            {"Dwindle", [] { setLayout("dwindle"); }, currentLayout == "dwindle"},
            {"Master", [] { setLayout("master"); }, currentLayout == "master"},
            {"Scrolling", [] { setLayout("scrolling"); }, currentLayout == "scrolling"},
            {"Monocle", [] { setLayout("monocle"); }, currentLayout == "monocle"},
        },
        selected);
}
