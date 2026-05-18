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

class ScreenrecordActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording CONSTANT)

public:
    explicit ScreenrecordActions(QObject *parent = nullptr) : QObject(parent), m_recording(isRecording()) {}

    bool recording() const { return m_recording; }

    Q_INVOKABLE void stop()
    {
        runShell(R"sh(pkill -INT -x wf-recorder && notify-send -u low -i camera 'Recording finished')sh", false);
    }

    Q_INVOKABLE void gif()
    {
        runShell(R"sh(sleep 0.18; wf-recorder -g "$(slurp)" -c gif -f "$HOME/Videos/screenrecord-$(date +%Y-%m-%dT%H-%M-%S).gif")sh");
    }

    Q_INVOKABLE void mp4()
    {
        runShell(R"sh(sleep 0.18; wf-recorder -g "$(slurp)" -f "$HOME/Videos/screenrecord-$(date +%Y-%m-%dT%H-%M-%S).mp4")sh");
    }

    Q_INVOKABLE void mp4Audio()
    {
        runShell(R"sh(sleep 0.18; wf-recorder -g "$(slurp)" -a -f "$HOME/Videos/screenrecord-$(date +%Y-%m-%dT%H-%M-%S).mp4")sh");
    }

private:
    bool m_recording;

    static bool isRecording()
    {
        QProcess process;
        process.start("pgrep", {"-x", "wf-recorder"});
        process.waitForFinished(500);
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }

    static void runShell(const QString &command, bool detach = true)
    {
        if (detach) {
            if (!QProcess::startDetached("sh", {"-c", command})) {
                qWarning().noquote() << "Failed to start screen recording command";
            }
        } else {
            QProcess::execute("sh", {"-c", command});
        }

        QGuiApplication::quit();
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
    width: 240
    height: actions.recording ? 96 : 220
    visible: true
    title: "Screen Record Menu"
    color: "#00000000"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property var buttons: actions.recording ? [stopButton] : [gifButton, mp4Button, mp4AudioButton]

    Component.onCompleted: {
        root.raise()
        root.requestActivate()
        buttons[0].forceActiveFocus()
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
                id: stopButton
                text: "Stop Recording"
                visible: actions.recording
                focus: actions.recording
                KeyNavigation.down: stopButton
                KeyNavigation.up: stopButton
                onClicked: actions.stop()
            }

            MenuButton {
                id: gifButton
                text: "GIF"
                visible: !actions.recording
                focus: !actions.recording
                KeyNavigation.down: mp4Button
                KeyNavigation.up: mp4AudioButton
                onClicked: actions.gif()
            }

            MenuButton {
                id: mp4Button
                text: "MP4"
                visible: !actions.recording
                KeyNavigation.down: mp4AudioButton
                KeyNavigation.up: gifButton
                onClicked: actions.mp4()
            }

            MenuButton {
                id: mp4AudioButton
                text: "MP4+Audio"
                visible: !actions.recording
                KeyNavigation.down: gifButton
                KeyNavigation.up: mp4Button
                onClicked: actions.mp4Audio()
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
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("screenrecord-menu");
    QGuiApplication::setDesktopFileName("screenrecord-menu");
    QQuickStyle::setStyle("Basic");

    ScreenrecordActions actions;
    EscapeFilter escapeFilter;
    app.installEventFilter(&escapeFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("actions", &actions);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QGuiApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadData(qml, QUrl("qrc:/ScreenrecordMenu.qml"));

    return app.exec();
}

#include "screenrecord-menu.moc"
