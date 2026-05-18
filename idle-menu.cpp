#include <QEvent>
#include <QFile>
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

class IdleActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool inhibited READ inhibited CONSTANT)

public:
    explicit IdleActions(QObject *parent = nullptr) : QObject(parent), m_inhibited(isInhibited()) {}

    bool inhibited() const { return m_inhibited; }

    Q_INVOKABLE void enable()
    {
        if (isInhibited()) {
            QGuiApplication::quit();
            return;
        }

        qint64 pid = 0;
        const bool started = QProcess::startDetached(
            "systemd-inhibit",
            {"--what=idle", "--who=idle-menu", "--why=User requested idle inhibit", "sleep", "infinity"},
            QString(),
            &pid
        );

        if (!started) {
            qWarning().noquote() << "Failed to start idle inhibitor";
        } else if (!writePid(pid)) {
            qWarning().noquote() << "Failed to write idle inhibitor pid" << pid;
        }

        QGuiApplication::quit();
    }

    Q_INVOKABLE void disable()
    {
        const qint64 pid = readPid();
        if (pid > 0) {
            QProcess::execute("kill", {QString::number(pid)});
        }
        QFile::remove(pidPath());
        QGuiApplication::quit();
    }

private:
    bool m_inhibited;

    static QString pidPath()
    {
        const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR", "/tmp");
        return runtimeDir + "/qt-idle-inhibit.pid";
    }

    static qint64 readPid()
    {
        QFile file(pidPath());
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            return 0;
        }

        bool ok = false;
        const qint64 pid = QString::fromLocal8Bit(file.readAll()).trimmed().toLongLong(&ok);
        return ok ? pid : 0;
    }

    static bool writePid(qint64 pid)
    {
        QFile file(pidPath());
        if (!file.open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) {
            return false;
        }

        file.write(QString::number(pid).toLocal8Bit());
        file.write("\n");
        return true;
    }

    static bool processExists(qint64 pid)
    {
        return pid > 0 && QProcess::execute("kill", {"-0", QString::number(pid)}) == 0;
    }

    static bool isInhibited()
    {
        const qint64 pid = readPid();
        if (processExists(pid)) {
            return true;
        }

        QFile::remove(pidPath());
        return false;
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
    height: 160
    visible: true
    title: "Idle Menu"
    color: "#00000000"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property var buttons: [enableButton, disableButton]

    Component.onCompleted: {
        root.raise()
        root.requestActivate()
        if (actions.inhibited) {
            enableButton.forceActiveFocus()
        } else {
            disableButton.forceActiveFocus()
        }
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
                id: enableButton
                text: "Keep Awake"
                selected: actions.inhibited
                KeyNavigation.down: disableButton
                KeyNavigation.up: disableButton
                onClicked: actions.enable()
            }

            MenuButton {
                id: disableButton
                text: "Allow Idle"
                selected: !actions.inhibited
                KeyNavigation.down: enableButton
                KeyNavigation.up: enableButton
                onClicked: actions.disable()
            }
        }
    }

    component MenuButton: Button {
        id: button
        property bool selected: false
        readonly property bool isFocusedOrHovered: hovered || visualFocus || activeFocus

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
                 : button.isFocusedOrHovered || button.selected ? Qt.lighter("#313244", 1.10)
                 : "#313244"
            border.width: 2
            border.color: button.isFocusedOrHovered || button.selected ? "#cdd6f4" : "#585b70"
        }
    }
}
)qml";

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("idle-menu");
    QGuiApplication::setDesktopFileName("idle-menu");
    QQuickStyle::setStyle("Basic");

    IdleActions actions;
    EscapeFilter escapeFilter;
    app.installEventFilter(&escapeFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("actions", &actions);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QGuiApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadData(qml, QUrl("qrc:/IdleMenu.qml"));

    return app.exec();
}

#include "idle-menu.moc"
