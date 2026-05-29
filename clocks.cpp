#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QtLogging>

#include <algorithm>

class ClockBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList zones READ zones CONSTANT)

public:
    explicit ClockBackend(QObject *parent = nullptr) : QObject(parent), m_zones(loadZones())
    {
        connect(&m_player, &QProcess::finished, this, [this] {
            if (m_loop && !m_currentFile.isEmpty()) {
                QTimer::singleShot(300, this, &ClockBackend::startCurrentSound);
            }
        });
    }

    ~ClockBackend() override { stopSound(); }

    QStringList zones() const { return m_zones; }

    Q_INVOKABLE QString systemTimeZone() const
    {
        const QString zone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
        return zone.isEmpty() ? QStringLiteral("UTC") : zone;
    }

    Q_INVOKABLE QString localTime() const
    {
        return QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("HH:mm"));
    }

    Q_INVOKABLE QString localDate() const
    {
        return QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dddd, MMMM d"));
    }

    Q_INVOKABLE QString zoneTitle(const QString &zoneId) const
    {
        if (zoneId == QStringLiteral("UTC")) {
            return zoneId;
        }

        QString title = zoneId.section('/', -1);
        if (title.isEmpty()) {
            title = zoneId;
        }
        title.replace('_', ' ');
        return title;
    }

    Q_INVOKABLE QString timeForZone(const QString &zoneId) const
    {
        const QTimeZone zone(zoneId.toUtf8());
        if (!zone.isValid()) {
            return QStringLiteral("--:--");
        }

        return QLocale().toString(QDateTime::currentDateTimeUtc().toTimeZone(zone), QStringLiteral("HH:mm"));
    }

    Q_INVOKABLE QString dateForZone(const QString &zoneId) const
    {
        const QTimeZone zone(zoneId.toUtf8());
        if (!zone.isValid()) {
            return QStringLiteral("Unknown zone");
        }

        return QLocale().toString(QDateTime::currentDateTimeUtc().toTimeZone(zone), QStringLiteral("ddd, MMM d"));
    }

    Q_INVOKABLE QString offsetForZone(const QString &zoneId) const
    {
        const QTimeZone zone(zoneId.toUtf8());
        if (!zone.isValid()) {
            return QStringLiteral("UTC");
        }

        const int offsetSeconds = zone.offsetFromUtc(QDateTime::currentDateTimeUtc());
        const int totalMinutes = std::abs(offsetSeconds) / 60;
        const int hours = totalMinutes / 60;
        const int minutes = totalMinutes % 60;
        return QStringLiteral("UTC%1%2:%3")
            .arg(offsetSeconds >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'));
    }

    Q_INVOKABLE bool playAlarm() { return playTone(QStringLiteral("alarm.wav"), true); }
    Q_INVOKABLE bool playTimer() { return playTone(QStringLiteral("timer.wav"), true); }
    Q_INVOKABLE bool previewAlarm() { return playTone(QStringLiteral("alarm.wav"), false); }
    Q_INVOKABLE bool previewTimer() { return playTone(QStringLiteral("timer.wav"), false); }

    Q_INVOKABLE void stopSound()
    {
        m_loop = false;
        m_currentFile.clear();
        if (m_player.state() != QProcess::NotRunning) {
            m_player.kill();
            m_player.waitForFinished(250);
        }
    }

private:
    QStringList m_zones;
    QProcess m_player;
    QString m_currentFile;
    bool m_loop = false;

    static QStringList loadZones()
    {
        QStringList zones;
        zones.reserve(QTimeZone::availableTimeZoneIds().size());
        for (const QByteArray &id : QTimeZone::availableTimeZoneIds()) {
            const QString zone = QString::fromUtf8(id);
            if (!zone.startsWith(QStringLiteral("posix/")) && !zone.startsWith(QStringLiteral("right/"))) {
                zones.push_back(zone);
            }
        }

        zones.removeDuplicates();
        zones.sort(Qt::CaseInsensitive);
        if (!zones.contains(QStringLiteral("UTC"))) {
            zones.prepend(QStringLiteral("UTC"));
        }
        return zones;
    }

    QString resolveSound(const QString &fileName) const
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList dirs = {
            QDir(appDir).filePath(QStringLiteral("sound")),
            QDir(appDir).filePath(QStringLiteral("../share/clocks/sound")),
#ifdef CLOCKS_SOUND_DIR
            QString::fromUtf8(CLOCKS_SOUND_DIR),
#endif
        };

        for (const QString &dir : dirs) {
            const QString path = QDir::cleanPath(QDir(dir).filePath(fileName));
            if (QFileInfo::exists(path)) {
                return path;
            }
        }

        return {};
    }

    bool playTone(const QString &fileName, bool loop)
    {
        const QString file = resolveSound(fileName);
        if (file.isEmpty()) {
            qWarning().noquote() << "Missing sound file" << fileName;
            return false;
        }

        m_loop = loop;
        m_currentFile = file;
        startCurrentSound();
        return true;
    }

    void startCurrentSound()
    {
        if (m_currentFile.isEmpty()) {
            return;
        }

        if (m_player.state() != QProcess::NotRunning) {
            m_player.kill();
            m_player.waitForFinished(250);
        }

        const QStringList playerNames = {
            QStringLiteral("pw-play"),
            QStringLiteral("paplay"),
            QStringLiteral("aplay"),
            QStringLiteral("ffplay"),
            QStringLiteral("mpv"),
        };

        QString program;
        QString playerName;
        for (const QString &candidate : playerNames) {
            program = QStandardPaths::findExecutable(candidate);
            if (!program.isEmpty()) {
                playerName = candidate;
                break;
            }
        }

        if (program.isEmpty()) {
            qWarning().noquote() << "No audio player found. Install pw-play, paplay, aplay, ffplay, or mpv.";
            return;
        }

        QStringList args;
        if (playerName == QStringLiteral("ffplay")) {
            args << QStringLiteral("-nodisp") << QStringLiteral("-autoexit") << QStringLiteral("-loglevel")
                 << QStringLiteral("quiet") << m_currentFile;
        } else if (playerName == QStringLiteral("mpv")) {
            args << QStringLiteral("--no-terminal") << QStringLiteral("--really-quiet") << m_currentFile;
        } else {
            args << m_currentFile;
        }

        m_player.start(program, args);
        if (!m_player.waitForStarted(700)) {
            qWarning().noquote() << "Failed to start audio player" << playerName;
        }
    }
};

class EscapeFilter final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        const auto type = event->type();
        if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress || type == QEvent::KeyRelease) {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                QGuiApplication::quit();
                return true;
            }
        }

        return QObject::eventFilter(object, event);
    }
};

static constexpr auto qml = R"qml(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 900
    height: 640
    minimumWidth: 520
    minimumHeight: 540
    visible: true
    title: "Clocks"
    color: bg

    readonly property color bg: "#181825"
    readonly property color panel: "#1e1e2e"
    readonly property color card: "#313244"
    readonly property color cardHover: "#45475a"
    readonly property color border: "#585b70"
    readonly property color textColor: "#cdd6f4"
    readonly property color muted: "#a6adc8"
    readonly property color accent: "#89b4fa"
    readonly property color green: "#a6e3a1"
    readonly property color yellow: "#f9e2af"
    readonly property color red: "#f38ba8"
    readonly property color lavender: "#b4befe"
    readonly property var pageTitles: ["World", "Alarms", "Stopwatch", "Timer"]
    readonly property int fontSmall: 16
    readonly property int fontLarge: 24
    readonly property int fontDisplay: 56
    readonly property int fontStrong: Font.DemiBold
    readonly property int controlHeight: 52

    property int pageIndex: 0
    property int tick: 0
    property string ringingTitle: "Alarm"
    property string ringingMessage: ""
    property bool stopwatchRunning: false
    property double stopwatchStartedAt: 0
    property double stopwatchBaseMs: 0
    property int stopwatchTick: 0
    property bool timerRunning: false
    property int timerTotalSeconds: 0
    property int timerRemainingSeconds: 0
    property double timerTargetMs: 0

    ListModel { id: worldModel }
    ListModel { id: alarmModel }
    ListModel { id: lapModel }

    Component.onCompleted: {
        root.addWorldClock(backend.systemTimeZone())
        root.addWorldClock("UTC")
        root.addWorldClock("America/New_York")
        root.addWorldClock("Europe/London")
        root.addWorldClock("Asia/Tokyo")
        alarmModel.append({ hour: 7, minute: 30, label: "Morning", enabled: false, lastKey: "" })
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            root.tick += 1
            root.checkAlarms()
        }
    }

    Timer {
        interval: 50
        running: root.stopwatchRunning
        repeat: true
        onTriggered: root.stopwatchTick += 1
    }

    Timer {
        interval: 250
        running: root.timerRunning
        repeat: true
        onTriggered: root.updateTimer()
    }

    function pad(value) {
        return value < 10 ? "0" + value : "" + value
    }

    function alarmText(hour, minute) {
        return pad(hour) + ":" + pad(minute)
    }

    function alarmRemainingText(hour, minute) {
        let now = new Date()
        let alarm = new Date(now)
        alarm.setHours(hour, minute, 0, 0)
        if (alarm <= now) {
            alarm.setDate(alarm.getDate() + 1)
        }

        let totalMinutes = Math.ceil((alarm.getTime() - now.getTime()) / 60000)
        let hours = Math.floor(totalMinutes / 60)
        let minutes = totalMinutes % 60
        let parts = []
        if (hours > 0) {
            parts.push(hours + " " + (hours === 1 ? "hour" : "hours"))
        }
        if (minutes > 0 || hours === 0) {
            parts.push(minutes + " " + (minutes === 1 ? "minute" : "minutes"))
        }
        return "In " + parts.join(" ")
    }

    function formatMs(ms) {
        ms = Math.max(0, Math.floor(ms))
        let centis = Math.floor((ms % 1000) / 10)
        let totalSeconds = Math.floor(ms / 1000)
        let seconds = totalSeconds % 60
        let minutes = Math.floor(totalSeconds / 60) % 60
        let hours = Math.floor(totalSeconds / 3600)
        if (hours > 0) {
            return pad(hours) + ":" + pad(minutes) + ":" + pad(seconds) + "." + pad(centis)
        }
        return pad(minutes) + ":" + pad(seconds) + "." + pad(centis)
    }

    function formatDuration(seconds) {
        seconds = Math.max(0, Math.floor(seconds))
        let hours = Math.floor(seconds / 3600)
        let minutes = Math.floor((seconds % 3600) / 60)
        let secs = seconds % 60
        return pad(hours) + ":" + pad(minutes) + ":" + pad(secs)
    }

    function movePage(offset) {
        const count = root.pageTitles.length
        root.pageIndex = (root.pageIndex + offset + count) % count
        const tab = pageTabs.itemAt(root.pageIndex)
        if (tab) {
            tab.forceActiveFocus()
        }
    }

    function handlePageNavigationKey(event) {
        if (event.accepted || event.modifiers !== Qt.NoModifier || zoneCombo.popup.visible) {
            return
        }

        if (event.key === Qt.Key_Left || event.key === Qt.Key_Up || event.key === Qt.Key_H || event.key === Qt.Key_K) {
            root.movePage(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down || event.key === Qt.Key_L || event.key === Qt.Key_J) {
            root.movePage(1)
            event.accepted = true
        }
    }

    function stopwatchElapsedMs() {
        if (root.stopwatchRunning) {
            return root.stopwatchBaseMs + Date.now() - root.stopwatchStartedAt
        }
        return root.stopwatchBaseMs
    }

    function addWorldClock(zoneId) {
        if (!zoneId || zoneId.length === 0) {
            return
        }
        for (let i = 0; i < worldModel.count; ++i) {
            if (worldModel.get(i).zoneId === zoneId) {
                return
            }
        }
        worldModel.append({ zoneId: zoneId })
    }

    function addAlarm() {
        let label = alarmLabel.text.trim()
        alarmModel.append({
            hour: alarmTime.hour,
            minute: alarmTime.minute,
            label: label.length > 0 ? label : "Alarm",
            enabled: true,
            lastKey: ""
        })
        alarmLabel.text = ""
    }

    function checkAlarms() {
        let now = new Date()
        let key = now.getFullYear() + "-" + now.getMonth() + "-" + now.getDate() + "-" + now.getHours() + "-" + now.getMinutes()
        for (let i = 0; i < alarmModel.count; ++i) {
            let alarm = alarmModel.get(i)
            if (alarm.enabled && alarm.hour === now.getHours() && alarm.minute === now.getMinutes() && alarm.lastKey !== key) {
                alarmModel.setProperty(i, "lastKey", key)
                root.showRinging("Alarm", alarm.label + " at " + alarmText(alarm.hour, alarm.minute), true)
            }
        }
    }

    function showRinging(title, message, alarm) {
        root.ringingTitle = title
        root.ringingMessage = message
        if (alarm) {
            backend.playAlarm()
        } else {
            backend.playTimer()
        }
        ringingPopup.open()
        root.raise()
        root.requestActivate()
    }

    function startStopwatch() {
        root.stopwatchStartedAt = Date.now()
        root.stopwatchRunning = true
    }

    function pauseStopwatch() {
        root.stopwatchBaseMs = root.stopwatchElapsedMs()
        root.stopwatchRunning = false
    }

    function resetStopwatch() {
        root.stopwatchRunning = false
        root.stopwatchBaseMs = 0
        root.stopwatchStartedAt = 0
        lapModel.clear()
    }

    function addLap() {
        lapModel.insert(0, { number: lapModel.count + 1, elapsed: root.formatMs(root.stopwatchElapsedMs()) })
    }

    function setTimerPreset(minutes) {
        timerTime.setTime(0, minutes, 0)
    }

    function startTimer() {
        timerTime.commitText()
        let total = timerTime.totalSeconds
        if (total <= 0) {
            return
        }
        root.timerTotalSeconds = total
        root.timerRemainingSeconds = total
        root.timerTargetMs = Date.now() + total * 1000
        root.timerRunning = true
    }

    function pauseTimer() {
        root.timerRemainingSeconds = Math.max(0, Math.ceil((root.timerTargetMs - Date.now()) / 1000))
        root.timerRunning = false
    }

    function resumeTimer() {
        if (root.timerRemainingSeconds <= 0) {
            return
        }
        root.timerTargetMs = Date.now() + root.timerRemainingSeconds * 1000
        root.timerRunning = true
    }

    function resetTimer() {
        root.timerRunning = false
        root.timerTotalSeconds = 0
        root.timerRemainingSeconds = 0
    }

    function updateTimer() {
        root.timerRemainingSeconds = Math.max(0, Math.ceil((root.timerTargetMs - Date.now()) / 1000))
        if (root.timerRemainingSeconds <= 0) {
            root.timerRunning = false
            root.showRinging("Timer", "Countdown complete", false)
        }
    }

    Rectangle {
        id: appShell
        anchors.fill: parent
        color: root.bg
        focus: true

        Component.onCompleted: forceActiveFocus()

        Keys.priority: Keys.AfterItem
        Keys.onPressed: event => root.handlePageNavigationKey(event)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 18

            TabBar {
                id: pageTabs
                Layout.alignment: Qt.AlignHCenter
                currentIndex: root.pageIndex
                spacing: 6
                background: Rectangle {
                    color: root.panel
                    radius: 18
                    border.width: 1
                    border.color: root.border
                }

                onCurrentIndexChanged: root.pageIndex = currentIndex

                Repeater {
                    model: root.pageTitles
                    delegate: ClockTabButton { text: modelData }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.pageIndex

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 16

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                ComboBox {
                                    id: zoneCombo
                                    Layout.preferredWidth: Math.min(360, root.width - 180)
                                    Layout.minimumWidth: 220
                                    Layout.preferredHeight: root.controlHeight
                                    editable: true
                                    model: backend.zones
                                    currentIndex: Math.max(0, backend.zones.indexOf(backend.systemTimeZone()))
                                    font.pixelSize: root.fontSmall
                                    delegate: ItemDelegate {
                                        width: zoneCombo.width
                                        height: 42
                                        text: modelData
                                        hoverEnabled: true
                                        contentItem: Text {
                                            text: parent.text
                                            color: root.textColor
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                            font.pixelSize: root.fontSmall
                                        }
                                        background: Rectangle {
                                            color: parent.highlighted ? root.accent : (parent.hovered ? root.cardHover : root.card)
                                        }
                                    }
                                    contentItem: Text {
                                        leftPadding: 12
                                        rightPadding: 28
                                        text: zoneCombo.displayText
                                        color: root.textColor
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font: zoneCombo.font
                                    }
                                    background: Rectangle {
                                        color: root.card
                                        radius: 14
                                        border.width: 1
                                        border.color: zoneCombo.activeFocus ? root.accent : root.border
                                    }
                                    popup: Popup {
                                        y: zoneCombo.height + 6
                                        width: zoneCombo.width
                                        height: Math.min(contentItem.implicitHeight + 2, root.height - zoneCombo.mapToItem(root.contentItem, 0, 0).y - zoneCombo.height - 36)
                                        padding: 1
                                        contentItem: ListView {
                                            clip: true
                                            implicitHeight: contentHeight
                                            model: zoneCombo.popup.visible ? zoneCombo.delegateModel : null
                                            currentIndex: zoneCombo.highlightedIndex
                                            ScrollBar.vertical: ScrollBar {
                                                policy: ScrollBar.AsNeeded
                                                contentItem: Rectangle {
                                                    implicitWidth: 6
                                                    radius: 3
                                                    color: root.border
                                                }
                                                background: Rectangle { color: root.panel }
                                            }
                                        }
                                        background: Rectangle {
                                            color: root.card
                                            radius: 14
                                            border.width: 1
                                            border.color: root.border
                                        }
                                    }
                                }

                                ActionButton {
                                    text: "Add"
                                    accentColor: root.accent
                                    onClicked: root.addWorldClock(zoneCombo.currentText)
                                }
                            }

                            Flickable {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                contentHeight: worldGrid.implicitHeight

                                GridLayout {
                                    id: worldGrid
                                    width: parent.width
                                    columns: root.width < 760 ? 1 : 2
                                    columnSpacing: 14
                                    rowSpacing: 14

                                    Repeater {
                                        model: worldModel
                                        delegate: Rectangle {
                                            Layout.preferredWidth: Math.max(220, (worldGrid.width - (worldGrid.columns - 1) * worldGrid.columnSpacing) / worldGrid.columns)
                                            Layout.preferredHeight: 132
                                            color: root.card
                                            radius: 24
                                            border.width: 1
                                            border.color: root.border

                                            ColumnLayout {
                                                anchors.fill: parent
                                                anchors.margins: 18
                                                spacing: 8

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 10

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2

                                                        Text {
                                                            text: backend.zoneTitle(zoneId)
                                                            color: root.textColor
                                                            font.pixelSize: root.fontLarge
                                                            font.weight: root.fontStrong
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }

                                                        Text {
                                                            text: zoneId
                                                            color: root.muted
                                                            font.pixelSize: root.fontSmall
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                    }

                                                    DestructiveButton {
                                                        visible: worldModel.count > 1
                                                        text: "Remove"
                                                        onClicked: worldModel.remove(index)
                                                    }
                                                }

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 12

                                                    Text {
                                                        text: backend.timeForZone(zoneId) + (root.tick >= 0 ? "" : "")
                                                        color: root.accent
                                                        font.pixelSize: root.fontLarge
                                                        font.weight: root.fontStrong
                                                    }

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        Text {
                                                            text: backend.dateForZone(zoneId) + (root.tick >= 0 ? "" : "")
                                                            color: root.textColor
                                                            font.pixelSize: root.fontSmall
                                                        }
                                                        Text {
                                                            text: backend.offsetForZone(zoneId) + (root.tick >= 0 ? "" : "")
                                                            color: root.muted
                                                            font.pixelSize: root.fontSmall
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 16

                            Rectangle {
                                id: alarmEditor
                                readonly property bool compact: width < 660

                                Layout.fillWidth: true
                                Layout.preferredHeight: alarmEditor.compact ? 154 : 92
                                color: root.card
                                radius: 24
                                border.width: 1
                                border.color: root.border

                                GridLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    columns: alarmEditor.compact ? 3 : 4
                                    columnSpacing: 10
                                    rowSpacing: 10

                                    TimeBox {
                                        id: alarmTime
                                        showSeconds: false
                                        maxHour: 23
                                        Layout.row: 0
                                        Layout.column: 0
                                        Layout.preferredWidth: 124
                                        Component.onCompleted: setTime(7, 30, 0)
                                    }
                                    TextField {
                                        id: alarmLabel
                                        Layout.row: alarmEditor.compact ? 1 : 0
                                        Layout.column: alarmEditor.compact ? 0 : 1
                                        Layout.columnSpan: alarmEditor.compact ? 3 : 1
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: root.controlHeight
                                        placeholderText: "Label"
                                        color: root.textColor
                                        placeholderTextColor: root.muted
                                        font.pixelSize: root.fontLarge
                                        background: Rectangle {
                                            color: root.panel
                                            radius: 14
                                            border.width: 1
                                            border.color: parent.activeFocus ? root.accent : root.border
                                        }
                                        onAccepted: root.addAlarm()
                                    }
                                    ActionButton {
                                        Layout.row: 0
                                        Layout.column: alarmEditor.compact ? 1 : 2
                                        text: "Add Alarm"
                                        accentColor: root.accent
                                        onClicked: root.addAlarm()
                                    }
                                    ActionButton {
                                        Layout.row: 0
                                        Layout.column: alarmEditor.compact ? 2 : 3
                                        text: "Preview"
                                        accentColor: root.lavender
                                        onClicked: backend.previewAlarm()
                                    }
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 12
                                clip: true
                                model: alarmModel
                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 94
                                    color: root.card
                                    radius: 24
                                    border.width: 1
                                    border.color: enabled ? root.accent : root.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 16

                                        Text {
                                            text: root.alarmText(hour, minute)
                                            color: enabled ? root.textColor : root.muted
                                            font.pixelSize: root.fontLarge
                                            font.weight: root.fontStrong
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            spacing: 4
                                            Text {
                                                text: label
                                                color: root.textColor
                                                font.pixelSize: root.fontSmall
                                                font.weight: root.fontStrong
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                text: enabled ? root.alarmRemainingText(hour, minute) + (root.tick >= 0 ? "" : "") : "Off"
                                                color: root.muted
                                                font.pixelSize: root.fontSmall
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }

                                        Switch {
                                            checked: enabled
                                            onToggled: alarmModel.setProperty(index, "enabled", checked)
                                        }

                                        DestructiveButton {
                                            text: "Remove"
                                            onClicked: alarmModel.remove(index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 18

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 210
                                color: root.card
                                radius: 28
                                border.width: 1
                                border.color: root.border

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 18

                                    Text {
                                        text: root.formatMs(root.stopwatchElapsedMs() + root.stopwatchTick * 0)
                                        color: root.accent
                                        font.pixelSize: root.fontDisplay
                                        font.weight: root.fontStrong
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.alignment: Qt.AlignHCenter
                                    }

                                    RowLayout {
                                        Layout.alignment: Qt.AlignHCenter
                                        spacing: 12
                                        ActionButton {
                                            text: root.stopwatchRunning ? "Pause" : "Start"
                                            accentColor: root.stopwatchRunning ? root.yellow : root.green
                                            onClicked: root.stopwatchRunning ? root.pauseStopwatch() : root.startStopwatch()
                                        }
                                        ActionButton {
                                            text: root.stopwatchRunning ? "Lap" : "Reset"
                                            accentColor: root.lavender
                                            onClicked: root.stopwatchRunning ? root.addLap() : root.resetStopwatch()
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: root.card
                                radius: 24
                                border.width: 1
                                border.color: root.border

                                ListView {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    clip: true
                                    model: lapModel
                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        height: 42
                                        color: root.panel
                                        radius: 12
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 14
                                            anchors.rightMargin: 14
                                            Text { text: "Lap " + number; color: root.muted; font.pixelSize: root.fontSmall; Layout.fillWidth: true }
                                            Text { text: elapsed; color: root.textColor; font.pixelSize: root.fontSmall; font.weight: root.fontStrong }
                                        }
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        visible: lapModel.count === 0
                                        text: "Laps appear here"
                                        color: root.muted
                                        font.pixelSize: root.fontSmall
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 18

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: root.card
                                radius: 28
                                border.width: 1
                                border.color: root.border

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width - 48, 560)
                                    spacing: 22

                                    Text {
                                        text: root.timerTotalSeconds > 0 ? root.formatDuration(root.timerRemainingSeconds) : "00:00:00"
                                        color: root.timerRunning ? root.green : root.accent
                                        font.pixelSize: root.fontDisplay
                                        font.weight: root.fontStrong
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.alignment: Qt.AlignHCenter
                                    }

                                    RowLayout {
                                        Layout.alignment: Qt.AlignHCenter
                                        spacing: 8
                                        TimeBox {
                                            id: timerTime
                                            showSeconds: true
                                            maxHour: 99
                                            Layout.preferredWidth: 174
                                            enabled: !root.timerRunning
                                            Component.onCompleted: setTime(0, 5, 0)
                                        }
                                    }

                                    RowLayout {
                                        Layout.alignment: Qt.AlignHCenter
                                        spacing: 8
                                        SmallButton { text: "5 min"; onClicked: root.setTimerPreset(5) }
                                        SmallButton { text: "10 min"; onClicked: root.setTimerPreset(10) }
                                        SmallButton { text: "25 min"; onClicked: root.setTimerPreset(25) }
                                        SmallButton { text: "Preview"; onClicked: backend.previewTimer() }
                                    }

                                    RowLayout {
                                        Layout.alignment: Qt.AlignHCenter
                                        spacing: 12
                                        ActionButton {
                                            text: root.timerRunning ? "Pause" : (root.timerRemainingSeconds > 0 ? "Resume" : "Start")
                                            accentColor: root.timerRunning ? root.yellow : root.green
                                            onClicked: root.timerRunning ? root.pauseTimer() : (root.timerRemainingSeconds > 0 ? root.resumeTimer() : root.startTimer())
                                        }
                                        ActionButton { text: "Reset"; accentColor: root.lavender; onClicked: root.resetTimer() }
                                    }
                                }
                            }
                        }
                    }
                }
        }
    }

    Popup {
        id: ringingPopup
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(root.width - 80, 430)
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        padding: 0
        background: Rectangle {
            color: root.panel
            radius: 28
            border.width: 2
            border.color: root.accent
        }

        ColumnLayout {
            width: parent.width
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 26
                spacing: 12

                Text {
                    text: root.ringingTitle
                    color: root.textColor
                    font.pixelSize: root.fontLarge
                    font.weight: root.fontStrong
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    text: root.ringingMessage
                    color: root.muted
                    font.pixelSize: root.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }

                ActionButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Stop"
                    accentColor: root.red
                    onClicked: {
                        backend.stopSound()
                        ringingPopup.close()
                    }
                }
            }
        }
    }

    component ClockTabButton: TabButton {
        id: tab
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: 0
        implicitWidth: Math.max(104, contentItem.implicitWidth + 34)
        implicitHeight: 42
        Keys.onPressed: event => {
            if (event.key === Qt.Key_H || event.key === Qt.Key_K) {
                root.movePage(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_L || event.key === Qt.Key_J) {
                root.movePage(1)
                event.accepted = true
            }
        }
        contentItem: Text {
            text: tab.text
            color: tab.checked ? root.bg : root.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.fontSmall
            font.weight: root.fontStrong
        }
        background: Rectangle {
            radius: 18
            color: tab.checked ? root.accent : (tab.hovered || tab.activeFocus ? root.cardHover : "transparent")
            border.width: 1
            border.color: tab.checked ? root.accent : "transparent"
        }
    }

    component ActionButton: Button {
        id: button
        property color accentColor: root.accent
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: 0
        implicitWidth: Math.max(106, contentItem.implicitWidth + 30)
        implicitHeight: root.controlHeight
        contentItem: Text {
            text: button.text
            color: root.bg
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.fontSmall
            font.weight: root.fontStrong
        }
        background: Rectangle {
            radius: 16
            color: button.down ? Qt.darker(button.accentColor, 1.2) : (button.hovered || button.activeFocus ? Qt.lighter(button.accentColor, 1.08) : button.accentColor)
        }
    }

    component DestructiveButton: ToolButton {
        id: button
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: 0
        implicitWidth: Math.max(84, contentItem.implicitWidth + 24)
        implicitHeight: 40
        contentItem: Text {
            text: button.text
            color: root.red
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.fontSmall
            font.weight: root.fontStrong
        }
        background: Rectangle {
            radius: 12
            color: button.hovered || button.activeFocus ? Qt.rgba(0.95, 0.55, 0.66, 0.14) : "transparent"
        }
    }

    component TimeBox: Rectangle {
        id: control
        property bool showSeconds: true
        property int maxHour: 23
        property int hour: 0
        property int minute: 0
        property int second: 0
        readonly property int totalSeconds: hour * 3600 + minute * 60 + second
        readonly property int digitCount: showSeconds ? 6 : 4
        readonly property var digitPositions: showSeconds ? [0, 1, 3, 4, 6, 7] : [0, 1, 3, 4]

        implicitWidth: showSeconds ? 174 : 124
        implicitHeight: root.controlHeight
        color: root.panel
        radius: 16
        opacity: enabled ? 1.0 : 0.55
        border.width: 1
        border.color: input.activeFocus ? root.accent : root.border

        function clamp(number, low, high) {
            return Math.max(low, Math.min(high, number))
        }

        function padded(number) {
            let text = "" + Math.max(0, number)
            while (text.length < 2) {
                text = "0" + text
            }
            return text.slice(-2)
        }

        function formatTime() {
            const base = control.padded(control.hour) + ":" + control.padded(control.minute)
            return control.showSeconds ? base + ":" + control.padded(control.second) : base
        }

        function parseText(text) {
            let digits = text.replace(/[^0-9]/g, "")
            while (digits.length < control.digitCount) {
                digits += "0"
            }
            digits = digits.slice(0, control.digitCount)
            return {
                hour: parseInt(digits.slice(0, 2), 10),
                minute: parseInt(digits.slice(2, 4), 10),
                second: control.showSeconds ? parseInt(digits.slice(4, 6), 10) : 0
            }
        }

        function normalizeParts(parts) {
            return {
                hour: control.clamp(isNaN(parts.hour) ? control.hour : parts.hour, 0, control.maxHour),
                minute: control.clamp(isNaN(parts.minute) ? control.minute : parts.minute, 0, 59),
                second: control.showSeconds ? control.clamp(isNaN(parts.second) ? control.second : parts.second, 0, 59) : 0
            }
        }

        function setTime(hour, minute, second) {
            const parts = control.normalizeParts({ hour: hour, minute: minute, second: second })
            control.hour = parts.hour
            control.minute = parts.minute
            control.second = parts.second
            input.text = control.formatTime()
        }

        function commitText() {
            const parts = control.normalizeParts(control.parseText(input.text))
            control.hour = parts.hour
            control.minute = parts.minute
            control.second = parts.second
            input.text = control.formatTime()
        }

        function activeDigit() {
            const start = Math.min(input.selectionStart, input.selectionEnd)
            if (input.selectionStart !== input.selectionEnd) {
                for (let i = 0; i < control.digitPositions.length; ++i) {
                    if (control.digitPositions[i] >= start) {
                        return i
                    }
                }
            }
            const cursor = input.cursorPosition
            for (let j = 0; j < control.digitPositions.length; ++j) {
                if (control.digitPositions[j] >= cursor) {
                    return j
                }
            }
            return control.digitPositions.length - 1
        }

        function selectDigit(index) {
            index = Math.max(0, Math.min(control.digitPositions.length - 1, index))
            const position = control.digitPositions[index]
            input.forceActiveFocus()
            input.select(position, position + 1)
        }

        function moveDigit(offset) {
            control.selectDigit(control.activeDigit() + offset)
        }

        function changeDigit(offset) {
            const digitIndex = control.activeDigit()
            control.commitText()
            const position = control.digitPositions[digitIndex]
            const currentDigit = parseInt(input.text.charAt(position), 10)

            for (let step = 1; step <= 10; ++step) {
                let nextDigit = (currentDigit + offset * step + 100) % 10
                let chars = input.text.split("")
                chars[position] = "" + nextDigit
                const parts = control.parseText(chars.join(""))
                if (parts.hour >= 0 && parts.hour <= control.maxHour && parts.minute >= 0 && parts.minute <= 59 && parts.second >= 0 && parts.second <= 59) {
                    control.hour = parts.hour
                    control.minute = parts.minute
                    control.second = parts.second
                    input.text = control.formatTime()
                    control.selectDigit(digitIndex)
                    return
                }
            }
        }

        onHourChanged: if (!input.activeFocus) input.text = control.formatTime()
        onMinuteChanged: if (!input.activeFocus) input.text = control.formatTime()
        onSecondChanged: if (!input.activeFocus) input.text = control.formatTime()

        onShowSecondsChanged: {
            if (!input.activeFocus) {
                input.text = control.formatTime()
            }
        }

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            text: control.formatTime()
            enabled: control.enabled
            color: root.textColor
            selectionColor: root.accent
            selectedTextColor: root.bg
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.fontLarge
            font.weight: root.fontStrong
            inputMask: control.showSeconds ? "00:00:00" : "00:00"
            inputMethodHints: Qt.ImhDigitsOnly
            selectByMouse: true
            activeFocusOnPress: true

            onTextEdited: {
                const parts = control.parseText(text)
                if (parts.hour >= 0 && parts.hour <= control.maxHour && parts.minute >= 0 && parts.minute <= 59 && parts.second >= 0 && parts.second <= 59) {
                    control.hour = parts.hour
                    control.minute = parts.minute
                    control.second = parts.second
                }
            }

            onEditingFinished: control.commitText()
            onActiveFocusChanged: if (!activeFocus) control.commitText()

            Keys.priority: Keys.BeforeItem
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_K) {
                    control.changeDigit(1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Down || event.key === Qt.Key_J) {
                    control.changeDigit(-1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Left || event.key === Qt.Key_H) {
                    control.moveDigit(-1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Right || event.key === Qt.Key_L) {
                    control.moveDigit(1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    control.commitText()
                    event.accepted = true
                }
            }
        }
    }

    component SmallButton: Button {
        id: button
        hoverEnabled: true
        padding: 0
        implicitWidth: 76
        implicitHeight: 36
        contentItem: Text {
            text: button.text
            color: root.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: root.fontSmall
            font.weight: root.fontStrong
        }
        background: Rectangle {
            radius: 14
            color: button.down ? Qt.darker(root.cardHover, 1.2) : (button.hovered || button.activeFocus ? root.cardHover : root.card)
            border.width: 1
            border.color: button.hovered || button.activeFocus ? root.accent : root.border
        }
    }
}
)qml";

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("clocks"));
    QGuiApplication::setDesktopFileName(QStringLiteral("clocks"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ClockBackend backend;
    EscapeFilter escapeFilter;
    app.installEventFilter(&escapeFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QGuiApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadData(qml, QUrl(QStringLiteral("qrc:/Clocks.qml")));

    return app.exec();
}

#include "clocks.moc"
