#include "imgui_app.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int windowWidth = 900;
constexpr int windowHeight = 640;

struct TimeValue {
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct Alarm {
    int hour = 0;
    int minute = 0;
    std::string label;
    bool enabled = false;
    std::string lastKey;
};

struct Lap {
    int number = 0;
    std::string elapsed;
};

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool containsInsensitive(const std::string &text, const std::string &needle)
{
    if (needle.empty()) {
        return true;
    }
    return lower(text).find(lower(needle)) != std::string::npos;
}

std::string twoDigits(int value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

std::string alarmText(int hour, int minute)
{
    return twoDigits(hour) + ":" + twoDigits(minute);
}

std::string formatDuration(int seconds)
{
    seconds = std::max(0, seconds);
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;
    return twoDigits(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(secs);
}

std::string formatMs(long long ms)
{
    ms = std::max(0LL, ms);
    const long long centis = (ms % 1000) / 10;
    const long long totalSeconds = ms / 1000;
    const long long seconds = totalSeconds % 60;
    const long long minutes = (totalSeconds / 60) % 60;
    const long long hours = totalSeconds / 3600;

    if (hours > 0) {
        return twoDigits(static_cast<int>(hours)) + ":" + twoDigits(static_cast<int>(minutes)) + ":" +
               twoDigits(static_cast<int>(seconds)) + "." + twoDigits(static_cast<int>(centis));
    }
    return twoDigits(static_cast<int>(minutes)) + ":" + twoDigits(static_cast<int>(seconds)) + "." +
           twoDigits(static_cast<int>(centis));
}

std::string formatWithStrftime(std::time_t timestamp, const char *format)
{
    std::tm tm{};
    localtime_r(&timestamp, &tm);
    char buffer[128];
    if (std::strftime(buffer, sizeof(buffer), format, &tm) == 0) {
        return {};
    }
    return buffer;
}

class ScopedTimeZone {
public:
    explicit ScopedTimeZone(const std::string &zone)
    {
        const char *old = std::getenv("TZ");
        if (old != nullptr) {
            m_old = old;
        }
        setenv("TZ", zone.c_str(), 1);
        tzset();
    }

    ~ScopedTimeZone()
    {
        if (m_old.has_value()) {
            setenv("TZ", m_old->c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

private:
    std::optional<std::string> m_old;
};

std::string formatInZone(const std::string &zone, const char *format)
{
    ScopedTimeZone scoped(zone);
    return formatWithStrftime(std::time(nullptr), format);
}

std::string zoneTitle(std::string zoneId)
{
    const auto slash = zoneId.find_last_of('/');
    if (slash != std::string::npos) {
        zoneId = zoneId.substr(slash + 1);
    }
    std::replace(zoneId.begin(), zoneId.end(), '_', ' ');
    return zoneId.empty() ? "UTC" : zoneId;
}

std::string offsetForZone(const std::string &zone)
{
    ScopedTimeZone scoped(zone);
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    const long offsetSeconds = tm.tm_gmtoff;
    const long totalMinutes = std::labs(offsetSeconds) / 60;
    const long hours = totalMinutes / 60;
    const long minutes = totalMinutes % 60;

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "UTC%c%02ld:%02ld", offsetSeconds >= 0 ? '+' : '-', hours, minutes);
    return buffer;
}

std::string systemTimeZone()
{
    std::error_code error;
    const auto link = std::filesystem::read_symlink("/etc/localtime", error);
    if (!error) {
        const std::string path = link.string();
        const std::string marker = "/zoneinfo/";
        const auto pos = path.find(marker);
        if (pos != std::string::npos) {
            return path.substr(pos + marker.size());
        }
    }

    const std::string timedate = ui::readCommandOutput("timedatectl show -p Timezone --value 2>/dev/null");
    return timedate.empty() ? "UTC" : timedate;
}

bool zoneFileAllowed(const std::filesystem::path &relative)
{
    if (relative.empty()) {
        return false;
    }
    const std::string path = relative.generic_string();
    if (path.rfind("posix/", 0) == 0 || path.rfind("right/", 0) == 0) {
        return false;
    }

    const std::string filename = relative.filename().string();
    return filename != "zone.tab" && filename != "zone1970.tab" && filename != "iso3166.tab" && filename != "leapseconds" &&
           filename != "tzdata.zi" && filename != "posixrules" && filename != "localtime";
}

std::vector<std::string> loadZones()
{
    std::set<std::string> zones;
    const std::filesystem::path root = "/usr/share/zoneinfo";
    std::error_code error;
    if (std::filesystem::exists(root, error)) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (!entry.is_regular_file(error)) {
                continue;
            }
            const auto relative = std::filesystem::relative(entry.path(), root, error);
            if (!error && zoneFileAllowed(relative)) {
                zones.insert(relative.generic_string());
            }
        }
    }
    zones.insert("UTC");

    std::vector<std::string> result(zones.begin(), zones.end());
    std::sort(result.begin(), result.end(), [](const std::string &left, const std::string &right) {
        return lower(left) < lower(right);
    });
    return result;
}

std::string remainingAlarmText(int hour, int minute)
{
    const std::time_t nowTime = std::time(nullptr);
    std::tm now{};
    localtime_r(&nowTime, &now);

    std::tm alarm = now;
    alarm.tm_hour = hour;
    alarm.tm_min = minute;
    alarm.tm_sec = 0;
    std::time_t alarmTime = std::mktime(&alarm);
    if (alarmTime <= nowTime) {
        alarm.tm_mday += 1;
        alarmTime = std::mktime(&alarm);
    }

    const int totalMinutes = std::max(1, static_cast<int>(std::ceil(std::difftime(alarmTime, nowTime) / 60.0)));
    const int hours = totalMinutes / 60;
    const int minutes = totalMinutes % 60;
    std::string text = "In ";
    if (hours > 0) {
        text += std::to_string(hours) + (hours == 1 ? " hour" : " hours");
        if (minutes > 0) {
            text += " ";
        }
    }
    if (minutes > 0 || hours == 0) {
        text += std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    }
    return text;
}

std::string alarmKeyForNow(std::tm now)
{
    return std::to_string(now.tm_year + 1900) + "-" + std::to_string(now.tm_mon) + "-" + std::to_string(now.tm_mday) + "-" +
           std::to_string(now.tm_hour) + "-" + std::to_string(now.tm_min);
}

bool parseTimeText(const char *text, bool showSeconds, int maxHour, TimeValue &value)
{
    std::string digits;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (std::isdigit(static_cast<unsigned char>(*cursor))) {
            digits += *cursor;
        }
    }

    const size_t wanted = showSeconds ? 6 : 4;
    while (digits.size() < wanted) {
        digits += '0';
    }
    digits.resize(wanted);

    const int hour = std::stoi(digits.substr(0, 2));
    const int minute = std::stoi(digits.substr(2, 2));
    const int second = showSeconds ? std::stoi(digits.substr(4, 2)) : 0;
    if (hour < 0 || hour > maxHour || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    value = {hour, minute, second};
    return true;
}

void setTimeText(char *buffer, size_t size, TimeValue value, bool showSeconds)
{
    if (showSeconds) {
        std::snprintf(buffer, size, "%02d:%02d:%02d", value.hour, value.minute, value.second);
    } else {
        std::snprintf(buffer, size, "%02d:%02d", value.hour, value.minute);
    }
}

void centerText(const std::string &text, float scale, ImVec4 color)
{
    ImGui::SetWindowFontScale(scale);
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
    ImGui::TextColored(color, "%s", text.c_str());
    ImGui::SetWindowFontScale(1.0f);
}

bool centeredAccentButton(const char *label, float width, ImVec4 color)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
    return ui::accentButton(label, ImVec2(width, 46.0f), color);
}

class AudioPlayer {
public:
    ~AudioPlayer() { stop(); }

    bool playAlarm() { return playTone("alarm.wav", true); }
    bool playTimer() { return playTone("timer.wav", true); }
    bool previewAlarm() { return playTone("alarm.wav", false); }
    bool previewTimer() { return playTone("timer.wav", false); }

    void stop()
    {
        m_loop = false;
        m_currentFile.clear();
        stopProcess();
    }

    void update()
    {
        if (m_pid <= 0) {
            return;
        }

        int status = 0;
        const pid_t result = waitpid(m_pid, &status, WNOHANG);
        if (result == 0) {
            return;
        }

        m_pid = -1;
        if (m_loop && !m_currentFile.empty()) {
            m_restartAt = Clock::now() + std::chrono::milliseconds(300);
        }
    }

    void maybeRestart()
    {
        if (m_pid <= 0 && m_loop && !m_currentFile.empty() && Clock::now() >= m_restartAt) {
            startCurrentSound();
        }
    }

private:
    pid_t m_pid = -1;
    std::string m_currentFile;
    bool m_loop = false;
    Clock::time_point m_restartAt = Clock::now();

    std::string resolveSound(const std::string &fileName) const
    {
        const std::filesystem::path appDir = ui::executableDir();
        const std::vector<std::filesystem::path> dirs = {
            appDir / "sound",
            appDir / "../share/clocks/sound",
#ifdef CLOCKS_SOUND_DIR
            CLOCKS_SOUND_DIR,
#endif
        };

        for (const auto &dir : dirs) {
            const auto path = std::filesystem::weakly_canonical(dir / fileName);
            std::error_code error;
            if (std::filesystem::exists(path, error)) {
                return path.string();
            }
        }
        return {};
    }

    bool playTone(const std::string &fileName, bool loop)
    {
        const std::string file = resolveSound(fileName);
        if (file.empty()) {
            std::cerr << "clocks: missing sound file " << fileName << '\n';
            return false;
        }

        stopProcess();
        m_loop = loop;
        m_currentFile = file;
        startCurrentSound();
        return true;
    }

    void stopProcess()
    {
        if (m_pid > 0) {
            kill(m_pid, SIGTERM);
            waitpid(m_pid, nullptr, 0);
            m_pid = -1;
        }
    }

    void startCurrentSound()
    {
        if (m_currentFile.empty()) {
            return;
        }

        stopProcess();

        const std::vector<std::string> playerNames = {"pw-play", "paplay", "aplay", "ffplay", "mpv"};
        std::string player;
        std::string program;
        for (const std::string &candidate : playerNames) {
            program = ui::findExecutable({candidate});
            if (!program.empty()) {
                player = candidate;
                break;
            }
        }

        if (program.empty()) {
            std::fputs("clocks: no audio player found. Install pw-play, paplay, aplay, ffplay, or mpv.\n", stderr);
            return;
        }

        std::vector<std::string> args;
        if (player == "ffplay") {
            args = {"-nodisp", "-autoexit", "-loglevel", "quiet", m_currentFile};
        } else if (player == "mpv") {
            args = {"--no-terminal", "--really-quiet", m_currentFile};
        } else {
            args = {m_currentFile};
        }

        const pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "clocks: fork failed: " << std::strerror(errno) << '\n';
            return;
        }
        if (pid == 0) {
            std::vector<char *> argv;
            argv.reserve(args.size() + 2);
            argv.push_back(const_cast<char *>(program.c_str()));
            for (std::string &arg : args) {
                argv.push_back(arg.data());
            }
            argv.push_back(nullptr);
            execv(program.c_str(), argv.data());
            _exit(127);
        }

        m_pid = pid;
    }
};

struct ClockState {
    std::vector<std::string> zones = loadZones();
    std::vector<std::string> worldZones;
    std::vector<Alarm> alarms;
    std::vector<Lap> laps;
    AudioPlayer audio;

    int page = 0;
    int selectedZone = 0;
    char zoneFilter[128] = "";
    char alarmLabel[128] = "";
    char alarmTimeText[16] = "07:30";
    char timerTimeText[16] = "00:05:00";
    TimeValue alarmTime = {7, 30, 0};
    TimeValue timerTime = {0, 5, 0};

    bool stopwatchRunning = false;
    Clock::time_point stopwatchStartedAt = Clock::now();
    long long stopwatchBaseMs = 0;

    bool timerRunning = false;
    int timerTotalSeconds = 0;
    int timerRemainingSeconds = 0;
    Clock::time_point timerTarget = Clock::now();

    bool ringing = false;
    std::string ringingTitle = "Alarm";
    std::string ringingMessage;
    std::time_t lastAlarmCheck = 0;

    ClockState()
    {
        const std::string systemZone = systemTimeZone();
        addWorldClock(systemZone);
        addWorldClock("UTC");
        addWorldClock("America/New_York");
        addWorldClock("Europe/London");
        addWorldClock("Asia/Tokyo");
        alarms.push_back({7, 30, "Morning", false, {}});

        const auto found = std::find(zones.begin(), zones.end(), systemZone);
        if (found != zones.end()) {
            selectedZone = static_cast<int>(std::distance(zones.begin(), found));
        }
    }

    void addWorldClock(const std::string &zone)
    {
        if (!zone.empty() && std::find(worldZones.begin(), worldZones.end(), zone) == worldZones.end()) {
            worldZones.push_back(zone);
        }
    }

    long long stopwatchElapsedMs() const
    {
        if (!stopwatchRunning) {
            return stopwatchBaseMs;
        }
        return stopwatchBaseMs + std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - stopwatchStartedAt).count();
    }

    void startStopwatch()
    {
        stopwatchStartedAt = Clock::now();
        stopwatchRunning = true;
    }

    void pauseStopwatch()
    {
        stopwatchBaseMs = stopwatchElapsedMs();
        stopwatchRunning = false;
    }

    void resetStopwatch()
    {
        stopwatchRunning = false;
        stopwatchBaseMs = 0;
        laps.clear();
    }

    void addLap()
    {
        laps.insert(laps.begin(), {static_cast<int>(laps.size()) + 1, formatMs(stopwatchElapsedMs())});
    }

    void setTimerPreset(int minutes)
    {
        timerTime = {0, minutes, 0};
        setTimeText(timerTimeText, sizeof(timerTimeText), timerTime, true);
    }

    void startTimer()
    {
        if (!parseTimeText(timerTimeText, true, 99, timerTime)) {
            setTimeText(timerTimeText, sizeof(timerTimeText), timerTime, true);
            return;
        }
        const int total = timerTime.hour * 3600 + timerTime.minute * 60 + timerTime.second;
        if (total <= 0) {
            return;
        }
        timerTotalSeconds = total;
        timerRemainingSeconds = total;
        timerTarget = Clock::now() + std::chrono::seconds(total);
        timerRunning = true;
    }

    void pauseTimer()
    {
        timerRemainingSeconds = std::max(0, static_cast<int>(std::ceil(std::chrono::duration<double>(timerTarget - Clock::now()).count())));
        timerRunning = false;
    }

    void resumeTimer()
    {
        if (timerRemainingSeconds <= 0) {
            return;
        }
        timerTarget = Clock::now() + std::chrono::seconds(timerRemainingSeconds);
        timerRunning = true;
    }

    void resetTimer()
    {
        timerRunning = false;
        timerTotalSeconds = 0;
        timerRemainingSeconds = 0;
    }

    void showRinging(std::string title, std::string message, bool alarm)
    {
        ringingTitle = std::move(title);
        ringingMessage = std::move(message);
        ringing = true;
        if (alarm) {
            audio.playAlarm();
        } else {
            audio.playTimer();
        }
    }

    void updateTimer()
    {
        if (!timerRunning) {
            return;
        }
        timerRemainingSeconds = std::max(0, static_cast<int>(std::ceil(std::chrono::duration<double>(timerTarget - Clock::now()).count())));
        if (timerRemainingSeconds <= 0) {
            timerRunning = false;
            showRinging("Timer", "Countdown complete", false);
        }
    }

    void checkAlarms()
    {
        const std::time_t nowTime = std::time(nullptr);
        if (nowTime == lastAlarmCheck) {
            return;
        }
        lastAlarmCheck = nowTime;

        std::tm now{};
        localtime_r(&nowTime, &now);
        const std::string key = alarmKeyForNow(now);
        for (Alarm &alarm : alarms) {
            if (alarm.enabled && alarm.hour == now.tm_hour && alarm.minute == now.tm_min && alarm.lastKey != key) {
                alarm.lastKey = key;
                showRinging("Alarm", alarm.label + " at " + alarmText(alarm.hour, alarm.minute), true);
            }
        }
    }

    void update()
    {
        checkAlarms();
        updateTimer();
        audio.update();
        audio.maybeRestart();
    }
};

void drawTabs(ClockState &state)
{
    constexpr const char *titles[] = {"World", "Alarms", "Stopwatch", "Timer"};
    const float tabWidth = 122.0f;
    const float totalWidth = tabWidth * 4.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f));
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        const bool active = state.page == i;
        const bool clicked = active ? ui::accentButton(titles[i], ImVec2(tabWidth, 42.0f)) : ui::subtleButton(titles[i], ImVec2(tabWidth, 42.0f));
        if (clicked) {
            state.page = i;
        }
        ImGui::PopID();
        if (i != 3) {
            ImGui::SameLine();
        }
    }
}

void drawWorldPage(ClockState &state)
{
    ImGui::PushItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 130.0f));
    ImGui::InputTextWithHint("##zone-filter", "Filter time zones", state.zoneFilter, sizeof(state.zoneFilter));
    ImGui::PopItemWidth();
    ImGui::SameLine();

    const std::string preview = state.selectedZone >= 0 && state.selectedZone < static_cast<int>(state.zones.size()) ? state.zones[state.selectedZone] : "UTC";
    ImGui::PushItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 120.0f));
    if (ImGui::BeginCombo("##zone-combo", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(state.zones.size()); ++i) {
            if (!containsInsensitive(state.zones[i], state.zoneFilter)) {
                continue;
            }
            const bool selected = i == state.selectedZone;
            if (ImGui::Selectable(state.zones[i].c_str(), selected)) {
                state.selectedZone = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ui::accentButton("Add", ImVec2(86.0f, 0.0f))) {
        state.addWorldClock(preview);
    }

    ImGui::Separator();
    ImGui::BeginChild("world-scroll", ImVec2(0.0f, 0.0f), false);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = width < 720.0f ? 1 : 2;
    const float cardWidth = columns == 1 ? width : (width - spacing) * 0.5f;
    int removeIndex = -1;

    for (int i = 0; i < static_cast<int>(state.worldZones.size()); ++i) {
        ImGui::PushID(i);
        ImGui::BeginChild("card", ImVec2(cardWidth, 132.0f), true);
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextColored(ui::colorText, "%s", zoneTitle(state.worldZones[i]).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(ui::colorMuted, "%s", state.worldZones[i].c_str());
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextColored(ui::colorAccent, "%s", formatInZone(state.worldZones[i], "%H:%M").c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(ui::colorText, "%s", formatInZone(state.worldZones[i], "%a, %b %d").c_str());
        ImGui::TextColored(ui::colorMuted, "%s", offsetForZone(state.worldZones[i]).c_str());
        ImGui::EndGroup();
        if (state.worldZones.size() > 1) {
            ImGui::SameLine(cardWidth - 104.0f);
            if (ui::destructiveButton("Remove", ImVec2(92.0f, 34.0f))) {
                removeIndex = i;
            }
        }
        ImGui::EndChild();
        ImGui::PopID();

        if (columns == 2 && i % 2 == 0 && i + 1 < static_cast<int>(state.worldZones.size())) {
            ImGui::SameLine();
        }
    }

    if (removeIndex >= 0) {
        state.worldZones.erase(state.worldZones.begin() + removeIndex);
    }
    ImGui::EndChild();
}

void drawAlarmsPage(ClockState &state)
{
    ImGui::BeginChild("alarm-editor", ImVec2(0.0f, 104.0f), true);
    ImGui::PushItemWidth(120.0f);
    if (ImGui::InputText("##alarm-time", state.alarmTimeText, sizeof(state.alarmTimeText), ImGuiInputTextFlags_CharsDecimal)) {
        parseTimeText(state.alarmTimeText, false, 23, state.alarmTime);
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 254.0f));
    ImGui::InputTextWithHint("##alarm-label", "Label", state.alarmLabel, sizeof(state.alarmLabel));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ui::accentButton("Add Alarm", ImVec2(118.0f, 0.0f))) {
        parseTimeText(state.alarmTimeText, false, 23, state.alarmTime);
        setTimeText(state.alarmTimeText, sizeof(state.alarmTimeText), state.alarmTime, false);
        std::string label = ui::trim(state.alarmLabel);
        state.alarms.push_back({state.alarmTime.hour, state.alarmTime.minute, label.empty() ? "Alarm" : label, true, {}});
        state.alarmLabel[0] = '\0';
    }
    ImGui::SameLine();
    if (ui::accentButton("Preview", ImVec2(100.0f, 0.0f), ui::colorLavender)) {
        state.audio.previewAlarm();
    }
    ImGui::TextColored(ui::colorMuted, "Use HH:MM. Enabled alarms ring once per matching minute.");
    ImGui::EndChild();

    ImGui::BeginChild("alarm-list", ImVec2(0.0f, 0.0f), false);
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(state.alarms.size()); ++i) {
        Alarm &alarm = state.alarms[i];
        ImGui::PushID(i);
        ImGui::BeginChild("alarm-card", ImVec2(0.0f, 94.0f), true);
        ImGui::Checkbox("##enabled", &alarm.enabled);
        ImGui::SameLine();
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextColored(alarm.enabled ? ui::colorText : ui::colorMuted, "%s", alarmText(alarm.hour, alarm.minute).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine(170.0f);
        ImGui::BeginGroup();
        ImGui::TextColored(ui::colorText, "%s", alarm.label.c_str());
        ImGui::TextColored(ui::colorMuted, "%s", alarm.enabled ? remainingAlarmText(alarm.hour, alarm.minute).c_str() : "Off");
        ImGui::EndGroup();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 100.0f);
        if (ui::destructiveButton("Remove", ImVec2(92.0f, 34.0f))) {
            removeIndex = i;
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    if (state.alarms.empty()) {
        centerText("No alarms yet", 1.0f, ui::colorMuted);
    }
    if (removeIndex >= 0) {
        state.alarms.erase(state.alarms.begin() + removeIndex);
    }
    ImGui::EndChild();
}

void drawStopwatchPage(ClockState &state)
{
    ImGui::BeginChild("stopwatch-display", ImVec2(0.0f, 210.0f), true);
    ImGui::Dummy(ImVec2(0.0f, 28.0f));
    centerText(formatMs(state.stopwatchElapsedMs()), 2.35f, ui::colorAccent);
    ImGui::Spacing();
    const float buttonWidth = 116.0f;
    const float groupWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - groupWidth) * 0.5f));
    if (ui::accentButton(state.stopwatchRunning ? "Pause" : "Start", ImVec2(buttonWidth, 46.0f), state.stopwatchRunning ? ui::colorYellow : ui::colorGreen)) {
        state.stopwatchRunning ? state.pauseStopwatch() : state.startStopwatch();
    }
    ImGui::SameLine();
    if (ui::accentButton(state.stopwatchRunning ? "Lap" : "Reset", ImVec2(buttonWidth, 46.0f), ui::colorLavender)) {
        state.stopwatchRunning ? state.addLap() : state.resetStopwatch();
    }
    ImGui::EndChild();

    ImGui::BeginChild("laps", ImVec2(0.0f, 0.0f), true);
    if (state.laps.empty()) {
        centerText("Laps appear here", 1.0f, ui::colorMuted);
    }
    for (const Lap &lap : state.laps) {
        ImGui::TextColored(ui::colorMuted, "Lap %d", lap.number);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 120.0f);
        ImGui::TextColored(ui::colorText, "%s", lap.elapsed.c_str());
        ImGui::Separator();
    }
    ImGui::EndChild();
}

void drawTimerPage(ClockState &state)
{
    ImGui::BeginChild("timer-panel", ImVec2(0.0f, 0.0f), true);
    ImGui::Dummy(ImVec2(0.0f, 70.0f));
    centerText(state.timerTotalSeconds > 0 ? formatDuration(state.timerRemainingSeconds) : "00:00:00", 2.35f,
               state.timerRunning ? ui::colorGreen : ui::colorAccent);
    ImGui::Spacing();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - 174.0f) * 0.5f));
    ImGui::PushItemWidth(174.0f);
    if (state.timerRunning) {
        ImGui::BeginDisabled();
    }
    if (ImGui::InputText("##timer-time", state.timerTimeText, sizeof(state.timerTimeText), ImGuiInputTextFlags_CharsDecimal)) {
        parseTimeText(state.timerTimeText, true, 99, state.timerTime);
    }
    if (state.timerRunning) {
        ImGui::EndDisabled();
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    const float smallWidth = 82.0f;
    const float presetWidth = smallWidth * 4.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - presetWidth) * 0.5f));
    if (ui::subtleButton("5 min", ImVec2(smallWidth, 36.0f))) {
        state.setTimerPreset(5);
    }
    ImGui::SameLine();
    if (ui::subtleButton("10 min", ImVec2(smallWidth, 36.0f))) {
        state.setTimerPreset(10);
    }
    ImGui::SameLine();
    if (ui::subtleButton("25 min", ImVec2(smallWidth, 36.0f))) {
        state.setTimerPreset(25);
    }
    ImGui::SameLine();
    if (ui::subtleButton("Preview", ImVec2(smallWidth, 36.0f))) {
        state.audio.previewTimer();
    }

    ImGui::Spacing();
    const float buttonWidth = 120.0f;
    const float groupWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - groupWidth) * 0.5f));
    const char *primary = state.timerRunning ? "Pause" : (state.timerRemainingSeconds > 0 ? "Resume" : "Start");
    if (ui::accentButton(primary, ImVec2(buttonWidth, 46.0f), state.timerRunning ? ui::colorYellow : ui::colorGreen)) {
        if (state.timerRunning) {
            state.pauseTimer();
        } else if (state.timerRemainingSeconds > 0) {
            state.resumeTimer();
        } else {
            state.startTimer();
        }
    }
    ImGui::SameLine();
    if (ui::accentButton("Reset", ImVec2(buttonWidth, 46.0f), ui::colorLavender)) {
        state.resetTimer();
    }
    ImGui::EndChild();
}

void drawRingingPopup(ClockState &state)
{
    if (state.ringing) {
        ImGui::OpenPopup("Ringing");
    }

    ImGui::SetNextWindowSize(ImVec2(430.0f, 190.0f), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Ringing", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        centerText(state.ringingTitle, 1.35f, ui::colorText);
        centerText(state.ringingMessage, 1.0f, ui::colorMuted);
        ImGui::Spacing();
        if (centeredAccentButton("Stop", 120.0f, ui::colorRed)) {
            state.audio.stop();
            state.ringing = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void drawClocks(ClockState &state, int width, int height, bool &running)
{
    if (!ImGui::GetIO().WantTextInput) {
        if (ui::pressedAny({ImGuiKey_Escape, ImGuiKey_Q})) {
            running = false;
        }
        if (ui::pressedAny({ImGuiKey_H, ImGuiKey_K, ImGuiKey_LeftArrow, ImGuiKey_UpArrow})) {
            state.page = (state.page + 3) % 4;
        }
        if (ui::pressedAny({ImGuiKey_L, ImGuiKey_J, ImGuiKey_RightArrow, ImGuiKey_DownArrow})) {
            state.page = (state.page + 1) % 4;
        }
    }

    state.update();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
    ImGui::Begin("clocks", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    drawTabs(state);
    ImGui::Spacing();

    ImGui::BeginChild("page", ImVec2(0.0f, 0.0f), false);
    if (state.page == 0) {
        drawWorldPage(state);
    } else if (state.page == 1) {
        drawAlarmsPage(state);
    } else if (state.page == 2) {
        drawStopwatchPage(state);
    } else {
        drawTimerPage(state);
    }
    ImGui::EndChild();

    drawRingingPopup(state);
    ImGui::End();
}

} // namespace

int main()
{
    ClockState state;
    ui::ImGuiApp app("clocks", windowWidth, windowHeight, 20.0f, true);
    return app.run([&](bool &running) { drawClocks(state, app.width(), app.height(), running); });
}
