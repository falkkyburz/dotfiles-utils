#include "imgui_app.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string readCurrentProfile()
{
    std::string output = ui::readCommandOutput(
        "busctl get-property net.hadess.PowerProfiles /net/hadess/PowerProfiles net.hadess.PowerProfiles ActiveProfile 2>/dev/null");
    const auto firstQuote = output.find('"');
    const auto lastQuote = output.rfind('"');
    if (firstQuote != std::string::npos && lastQuote != std::string::npos && lastQuote > firstQuote) {
        return output.substr(firstQuote + 1, lastQuote - firstQuote - 1);
    }
    if (!output.empty()) {
        std::fputs("power-menu: failed to parse current power profile\n", stderr);
    }
    return "balanced";
}

void setProfile(const char *profile)
{
    const std::string command = std::string("busctl set-property net.hadess.PowerProfiles /net/hadess/PowerProfiles ") +
                                "net.hadess.PowerProfiles ActiveProfile s " + ui::shellQuote(profile);
    if (!ui::commandSucceeded(command)) {
        std::fprintf(stderr, "power-menu: failed to set power profile %s\n", profile);
    }
}

} // namespace

int main()
{
    const std::string currentProfile = readCurrentProfile();
    int selected = 1;
    if (currentProfile == "performance") {
        selected = 0;
    } else if (currentProfile == "power-saver") {
        selected = 2;
    }

    return ui::runButtonMenu(
        "power-menu", 240, 220,
        {
            {"Performance", [] { setProfile("performance"); }, currentProfile == "performance"},
            {"Balanced", [] { setProfile("balanced"); }, currentProfile == "balanced"},
            {"Power Saver", [] { setProfile("power-saver"); }, currentProfile == "power-saver"},
        },
        selected);
}
