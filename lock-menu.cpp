#include <QGuiApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStringList>
#include <QUrl>
#include <QtLogging>

class PowerActions final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void lock() { run("hyprlock"); }
    Q_INVOKABLE void suspend() { run("systemctl", {"suspend"}); }
    Q_INVOKABLE void reboot() { run("systemctl", {"reboot"}); }
    Q_INVOKABLE void shutdown() { run("systemctl", {"poweroff"}); }

private:
    void run(const QString &program, const QStringList &arguments = {})
    {
        if (!QProcess::startDetached(program, arguments)) {
            qWarning().noquote() << "Failed to start" << program << arguments.join(' ');
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
    height: 280
    visible: true
    title: "Power Menu"
    color: "#00000000"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property var buttons: [lockButton, suspendButton, rebootButton, shutdownButton]

    Component.onCompleted: {
        root.raise()
        root.requestActivate()
        lockButton.forceActiveFocus()
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
                id: lockButton
                text: "Lock"
                focus: true
                KeyNavigation.down: suspendButton
                KeyNavigation.up: shutdownButton
                onClicked: actions.lock()
            }

            MenuButton {
                id: suspendButton
                text: "Suspend"
                KeyNavigation.down: rebootButton
                KeyNavigation.up: lockButton
                onClicked: actions.suspend()
            }

            MenuButton {
                id: rebootButton
                text: "Reboot"
                KeyNavigation.down: shutdownButton
                KeyNavigation.up: suspendButton
                onClicked: actions.reboot()
            }

            MenuButton {
                id: shutdownButton
                text: "Shutdown"
                KeyNavigation.down: lockButton
                KeyNavigation.up: rebootButton
                onClicked: actions.shutdown()
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
    QGuiApplication::setApplicationName("lock-menu");
    QGuiApplication::setDesktopFileName("lock-menu");
    QQuickStyle::setStyle("Basic");

    PowerActions actions;
    EscapeFilter escapeFilter;
    app.installEventFilter(&escapeFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("actions", &actions);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QGuiApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadData(qml, QUrl("qrc:/Main.qml"));

    return app.exec();
}

#include "lock-menu.moc"
