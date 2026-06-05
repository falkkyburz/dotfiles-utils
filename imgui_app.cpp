#include "imgui_app.h"

#include <SDL.h>
#include <SDL_opengles2.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits.h>
#include <memory>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ui {
namespace {

SDL_Window *toWindow(void *window)
{
    return static_cast<SDL_Window *>(window);
}

SDL_GLContext toContext(void *context)
{
    return static_cast<SDL_GLContext>(context);
}

void applyStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(22.0f, 22.0f);
    style.ItemSpacing = ImVec2(0.0f, 10.0f);
    style.WindowRounding = 0.0f;
    style.FrameRounding = 10.0f;
    style.FrameBorderSize = 2.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 16.0f;
    style.Colors[ImGuiCol_WindowBg] = colorBackground;
    style.Colors[ImGuiCol_PopupBg] = colorPanel;
    style.Colors[ImGuiCol_Text] = colorText;
    style.Colors[ImGuiCol_TextDisabled] = colorMuted;
    style.Colors[ImGuiCol_Button] = colorButton;
    style.Colors[ImGuiCol_ButtonHovered] = colorButtonHover;
    style.Colors[ImGuiCol_ButtonActive] = colorButtonDown;
    style.Colors[ImGuiCol_FrameBg] = colorButton;
    style.Colors[ImGuiCol_FrameBgHovered] = colorButtonHover;
    style.Colors[ImGuiCol_FrameBgActive] = colorButtonDown;
    style.Colors[ImGuiCol_Header] = colorButtonHover;
    style.Colors[ImGuiCol_HeaderHovered] = colorAccent;
    style.Colors[ImGuiCol_HeaderActive] = colorAccent;
    style.Colors[ImGuiCol_Border] = colorBorder;
    style.Colors[ImGuiCol_CheckMark] = colorAccent;
    style.Colors[ImGuiCol_SliderGrab] = colorAccent;
    style.Colors[ImGuiCol_Tab] = colorPanel;
    style.Colors[ImGuiCol_TabHovered] = colorButtonHover;
    style.Colors[ImGuiCol_TabSelected] = colorAccent;
}

void loadFont(float fontSize)
{
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    if (io.Fonts->AddFontFromFileTTF("/usr/share/fonts/noto/NotoSans-Regular.ttf", fontSize, &fontConfig) == nullptr) {
        fontConfig.SizePixels = fontSize;
        io.Fonts->AddFontDefault(&fontConfig);
    }
}

std::vector<char *> argvFor(const std::string &program, const std::vector<std::string> &args)
{
    std::vector<char *> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char *>(program.c_str()));
    for (const std::string &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

struct PipeCloser {
    void operator()(FILE *file) const
    {
        if (file != nullptr) {
            pclose(file);
        }
    }
};

} // namespace

ImGuiApp::ImGuiApp(const char *title, int width, int height, float fontSize, bool resizable) : m_width(width), m_height(height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << '\n';
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALWAYS_ON_TOP;
    if (resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    } else {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    SDL_Window *window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (window == nullptr) {
        std::cerr << "SDL window failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        std::cerr << "SDL GL context failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }

    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    loadFont(fontSize);
    applyStyle();

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    m_window = window;
    m_glContext = glContext;
    m_valid = true;
}

ImGuiApp::~ImGuiApp()
{
    if (!m_window) {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(toContext(m_glContext));
    SDL_DestroyWindow(toWindow(m_window));
    SDL_Quit();
}

void ImGuiApp::setTitle(const char *title)
{
    SDL_SetWindowTitle(toWindow(m_window), title);
}

int ImGuiApp::run(const std::function<void(bool &running)> &drawFrame)
{
    if (!m_valid) {
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                m_width = event.window.data1;
                m_height = event.window.data2;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        drawFrame(running);

        ImGui::Render();
        glViewport(0, 0, m_width, m_height);
        glClearColor(colorBackground.x, colorBackground.y, colorBackground.z, colorBackground.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(toWindow(m_window));
    }

    return 0;
}

bool pressedAny(std::initializer_list<ImGuiKey> keys)
{
    for (const ImGuiKey key : keys) {
        if (ImGui::IsKeyPressed(key, false)) {
            return true;
        }
    }
    return false;
}

bool accentButton(const char *label, const ImVec2 &size, ImVec4 accent)
{
    ImVec4 hovered = ImVec4(std::min(accent.x * 1.08f, 1.0f), std::min(accent.y * 1.08f, 1.0f),
                            std::min(accent.z * 1.08f, 1.0f), accent.w);
    ImVec4 active = ImVec4(accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, accent.w);
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, colorBackground);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool subtleButton(const char *label, const ImVec2 &size, bool active)
{
    ImGui::PushStyleColor(ImGuiCol_Button, active ? colorButtonHover : colorButton);
    ImGui::PushStyleColor(ImGuiCol_Border, active ? colorText : colorBorder);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return clicked;
}

bool destructiveButton(const char *label, const ImVec2 &size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(colorRed.x, colorRed.y, colorRed.z, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colorRed.x, colorRed.y, colorRed.z, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colorRed.x, colorRed.y, colorRed.z, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_Text, colorRed);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

int runButtonMenu(const char *title, int width, int height, std::vector<ButtonMenuItem> items, int initialSelected)
{
    if (items.empty()) {
        return 0;
    }

    ImGuiApp app(title, width, height);
    int selected = std::clamp(initialSelected, 0, static_cast<int>(items.size()) - 1);
    return app.run([&](bool &running) {
        if (pressedAny({ImGuiKey_Escape, ImGuiKey_Q})) {
            running = false;
        }
        if (pressedAny({ImGuiKey_J, ImGuiKey_DownArrow})) {
            selected = (selected + 1) % static_cast<int>(items.size());
        }
        if (pressedAny({ImGuiKey_K, ImGuiKey_UpArrow})) {
            selected = (selected + static_cast<int>(items.size()) - 1) % static_cast<int>(items.size());
        }
        if (pressedAny({ImGuiKey_Enter, ImGuiKey_KeypadEnter})) {
            items[selected].activate();
            running = false;
        }

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
        ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float buttonSpacing = ImGui::GetStyle().ItemSpacing.y * static_cast<float>(items.size() - 1);
        const float buttonHeight = (available.y - buttonSpacing) / static_cast<float>(items.size());

        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const bool active = i == selected || items[i].marked;
            ImGui::PushID(i);
            if (subtleButton(items[i].label.c_str(), ImVec2(available.x, buttonHeight), active)) {
                items[i].activate();
                running = false;
            }
            if (ImGui::IsItemHovered()) {
                selected = i;
            }
            ImGui::PopID();
        }

        ImGui::End();
    });
}

std::string trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string shellQuote(std::string_view text)
{
    std::string quoted = "'";
    for (const char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

bool runDetachedShell(std::string_view command)
{
    const std::string commandString(command);
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << '\n';
        return false;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", commandString.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    return true;
}

int runBlockingShell(std::string_view command)
{
    const std::string commandString(command);
    return std::system(commandString.c_str());
}

bool commandSucceeded(std::string_view command)
{
    const int status = runBlockingShell(command);
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string readCommandOutput(std::string_view command)
{
    const std::string commandString(command);
    std::unique_ptr<FILE, PipeCloser> pipe(popen(commandString.c_str(), "r"));
    if (!pipe) {
        return {};
    }

    std::string output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        output += buffer;
    }
    return trim(output);
}

std::string executableDir()
{
    char path[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) {
        return ".";
    }
    path[length] = '\0';
    return std::filesystem::path(path).parent_path().string();
}

std::string findExecutable(const std::vector<std::string> &names)
{
    const char *pathValue = std::getenv("PATH");
    if (pathValue == nullptr) {
        return {};
    }

    std::stringstream pathStream(pathValue);
    std::string dir;
    while (std::getline(pathStream, dir, ':')) {
        if (dir.empty()) {
            dir = ".";
        }
        for (const std::string &name : names) {
            const std::filesystem::path candidate = std::filesystem::path(dir) / name;
            if (access(candidate.c_str(), X_OK) == 0) {
                return candidate.string();
            }
        }
    }

    return {};
}

bool startDetachedProcess(const std::string &program, const std::vector<std::string> &args, int *pidOut)
{
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork failed: " << std::strerror(errno) << '\n';
        return false;
    }
    if (pid == 0) {
        setsid();
        std::vector<char *> argv = argvFor(program, args);
        execvp(program.c_str(), argv.data());
        _exit(127);
    }

    if (pidOut != nullptr) {
        *pidOut = static_cast<int>(pid);
    }
    return true;
}

} // namespace ui
