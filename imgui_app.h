#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace ui {

constexpr ImVec4 colorBackground = ImVec4(0.094f, 0.094f, 0.145f, 1.0f); // #181825
constexpr ImVec4 colorPanel = ImVec4(0.118f, 0.118f, 0.180f, 1.0f);      // #1e1e2e
constexpr ImVec4 colorButton = ImVec4(0.192f, 0.196f, 0.267f, 1.0f);     // #313244
constexpr ImVec4 colorButtonHover = ImVec4(0.271f, 0.278f, 0.353f, 1.0f); // #45475a
constexpr ImVec4 colorButtonDown = ImVec4(0.157f, 0.161f, 0.220f, 1.0f);
constexpr ImVec4 colorBorder = ImVec4(0.345f, 0.357f, 0.439f, 1.0f);     // #585b70
constexpr ImVec4 colorText = ImVec4(0.804f, 0.839f, 0.957f, 1.0f);       // #cdd6f4
constexpr ImVec4 colorMuted = ImVec4(0.651f, 0.678f, 0.784f, 1.0f);      // #a6adc8
constexpr ImVec4 colorAccent = ImVec4(0.537f, 0.706f, 0.980f, 1.0f);     // #89b4fa
constexpr ImVec4 colorGreen = ImVec4(0.651f, 0.890f, 0.631f, 1.0f);      // #a6e3a1
constexpr ImVec4 colorYellow = ImVec4(0.976f, 0.886f, 0.686f, 1.0f);     // #f9e2af
constexpr ImVec4 colorRed = ImVec4(0.953f, 0.545f, 0.659f, 1.0f);        // #f38ba8
constexpr ImVec4 colorLavender = ImVec4(0.706f, 0.745f, 0.937f, 1.0f);   // #b4befe

class ImGuiApp {
public:
    ImGuiApp(const char *title, int width, int height, float fontSize = 34.0f, bool resizable = false);
    ~ImGuiApp();

    ImGuiApp(const ImGuiApp &) = delete;
    ImGuiApp &operator=(const ImGuiApp &) = delete;

    bool valid() const { return m_valid; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    void setTitle(const char *title);

    int run(const std::function<void(bool &running)> &drawFrame);

private:
    void *m_window = nullptr;
    void *m_glContext = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_valid = false;
};

struct ButtonMenuItem {
    std::string label;
    std::function<void()> activate;
    bool marked = false;
};

bool pressedAny(std::initializer_list<ImGuiKey> keys);
bool accentButton(const char *label, const ImVec2 &size, ImVec4 accent = colorAccent);
bool subtleButton(const char *label, const ImVec2 &size, bool active = false);
bool destructiveButton(const char *label, const ImVec2 &size = ImVec2(0.0f, 0.0f));
int runButtonMenu(const char *title, int width, int height, std::vector<ButtonMenuItem> items, int initialSelected = 0);

std::string trim(std::string text);
std::string shellQuote(std::string_view text);
bool runDetachedShell(std::string_view command);
int runBlockingShell(std::string_view command);
bool commandSucceeded(std::string_view command);
std::string readCommandOutput(std::string_view command);
std::string executableDir();
std::string findExecutable(const std::vector<std::string> &names);

bool startDetachedProcess(const std::string &program, const std::vector<std::string> &args, int *pidOut = nullptr);

} // namespace ui
