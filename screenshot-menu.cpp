#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtLogging>

#include <cstdio>

class ScreenshotActions final : public QObject {
    Q_OBJECT

public:
    explicit ScreenshotActions(QObject *parent = nullptr) : QObject(parent), m_focusedWindow(readFocusedWindow()) {}

    Q_INVOKABLE void region()
    {
        runShell(regionCommand(true, false));
    }

    static bool runRegionWithoutGui(bool includeCursor)
    {
        return startDetachedCommand(regionCommand(false, includeCursor));
    }

    Q_INVOKABLE void focusedWindow()
    {
        if (m_focusedWindow.isEmpty() || m_focusedWindow == "null") {
            qWarning().noquote() << "No focused window address available";
            QGuiApplication::quit();
            return;
        }

        runShell(QString(R"sh(sleep 0.18; geom="$(hyprctl clients -j | jq -r --arg addr '%1' '.[] | select(.address == $addr) | "\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"')"; [ -n "$geom" ] && [ "$geom" != "null" ] || exit 1; grim -g "$geom" - | satty --filename -)sh").arg(m_focusedWindow));
    }

    Q_INVOKABLE void fullScreen()
    {
        runShell(R"sh(sleep 0.18; mon="$(hyprctl monitors -j | jq -r '.[] | select(.focused == true) | .name' | head -n1)"; [ -n "$mon" ] && [ "$mon" != "null" ] || exit 1; grim -o "$mon" - | satty --filename -)sh");
    }

private:
    QString m_focusedWindow;

    static QString regionCommand(bool menuDelay, bool includeCursor)
    {
        return QStringLiteral("%1screenfreeze%2 & freeze_pid=$!; trap 'kill \"$freeze_pid\" 2>/dev/null' EXIT; sleep 0.15; geom=\"$(slurp)\" || exit 0; tmp=\"$(mktemp --suffix=.png)\" || exit 1; grim%3 -g \"$geom\" \"$tmp\" || exit 1; kill \"$freeze_pid\" 2>/dev/null; wait \"$freeze_pid\" 2>/dev/null; trap 'rm -f \"$tmp\"' EXIT; satty --filename \"$tmp\"")
            .arg(menuDelay ? QStringLiteral("sleep 0.18; ") : QString{},
                 includeCursor ? QStringLiteral(" --cursor") : QString{},
                 includeCursor ? QStringLiteral(" -c") : QString{});
    }

    static QString readFocusedWindow()
    {
        QProcess process;
        process.start("sh", {"-c", "hyprctl activewindow -j | jq -r '.address // empty'"});
        if (!process.waitForFinished(1000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return {};
        }

        return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }

    static void runShell(const QString &command)
    {
        if (!startDetachedCommand(command)) {
            qWarning().noquote() << "Failed to start screenshot command";
        }
        QGuiApplication::quit();
    }

    static bool startDetachedCommand(const QString &command)
    {
        return QProcess::startDetached("sh", {"-c", command});
    }
};

static void printUsage()
{
    std::fputs("Usage: screenshot-menu [--region]\n\n"
               "Options:\n"
               "  -r, --region  Capture a region without opening the GUI.\n"
               "  -c, --cursor  Include the cursor when used with --region.\n"
               "  -h, --help    Show this help text.\n",
               stdout);
}

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
    width: 240
    height: 220
    visible: true
    title: "Screenshot Menu"
    color: "#00000000"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property var buttons: [regionButton, windowButton, fullScreenButton]

    Component.onCompleted: {
        root.raise()
        root.requestActivate()
        regionButton.forceActiveFocus()
    }

    function focusByOffset(offset) {
        let index = buttons.findIndex(button => button.activeFocus)
        if (index < 0) {
            index = 0
        }
        buttons[(index + offset + buttons.length) % buttons.length].forceActiveFocus()
    }

    Shortcut {
        sequence: "J"
        context: Qt.ApplicationShortcut
        onActivated: root.focusByOffset(1)
    }

    Shortcut {
        sequence: "K"
        context: Qt.ApplicationShortcut
        onActivated: root.focusByOffset(-1)
    }

    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        onActivated: Qt.quit()
    }

    Rectangle {
        anchors.fill: parent
        color: "#181825"
        radius: 0

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 10

            MenuButton {
                id: regionButton
                text: "Region"
                focus: true
                KeyNavigation.down: windowButton
                KeyNavigation.up: fullScreenButton
                onClicked: actions.region()
            }

            MenuButton {
                id: windowButton
                text: "Focused Window"
                KeyNavigation.down: fullScreenButton
                KeyNavigation.up: regionButton
                onClicked: actions.focusedWindow()
            }

            MenuButton {
                id: fullScreenButton
                text: "Full Screen"
                KeyNavigation.down: regionButton
                KeyNavigation.up: windowButton
                onClicked: actions.fullScreen()
            }
        }
    }

    component MenuButton: Button {
        id: button
        readonly property bool isFocusedOrHovered: hovered || visualFocus || activeFocus || focus

        Layout.fillWidth: true
        Layout.preferredHeight: 46
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: 17
        font.weight: Font.DemiBold

        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()

        contentItem: Text {
            text: button.text
            color: "#cdd6f4"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: 16
            color: button.down ? Qt.darker("#313244", 1.22)
                 : button.isFocusedOrHovered ? Qt.lighter("#313244", 1.10)
                 : "#313244"
            border.width: 2
            border.color: button.isFocusedOrHovered ? "#cdd6f4" : "#585b70"
        }
    }
}
)qml";

int main(int argc, char *argv[])
{
    bool region = false;
    bool includeCursor = false;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("-h") || arg == QStringLiteral("--help")) {
            printUsage();
            return 0;
        }
        if (arg == QStringLiteral("-r") || arg == QStringLiteral("--region")) {
            region = true;
            continue;
        }
        if (arg == QStringLiteral("-c") || arg == QStringLiteral("--cursor")) {
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
        return ScreenshotActions::runRegionWithoutGui(includeCursor) ? 0 : 1;
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("screenshot-menu");
    QGuiApplication::setDesktopFileName("screenshot-menu");
    QQuickStyle::setStyle("Basic");

    ScreenshotActions actions;
    EscapeFilter escapeFilter;
    app.installEventFilter(&escapeFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("actions", &actions);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QGuiApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadData(qml, QUrl("qrc:/ScreenshotMenu.qml"));

    return app.exec();
}

#include "screenshot-menu.moc"
