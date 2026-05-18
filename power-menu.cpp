#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtLogging>

namespace {
constexpr auto service = "org.freedesktop.UPower.PowerProfiles";
constexpr auto path = "/org/freedesktop/UPower/PowerProfiles";
constexpr auto interface = "org.freedesktop.UPower.PowerProfiles";

QDBusInterface propertiesInterface()
{
    return QDBusInterface(service, path, "org.freedesktop.DBus.Properties", QDBusConnection::systemBus());
}
}

class ProfileActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentProfile READ currentProfile CONSTANT)

public:
    explicit ProfileActions(QObject *parent = nullptr) : QObject(parent), m_currentProfile(readCurrentProfile()) {}

    QString currentProfile() const { return m_currentProfile; }

    Q_INVOKABLE void setProfile(const QString &profile)
    {
        auto properties = propertiesInterface();
        const QDBusReply<void> reply = properties.call("Set", interface, "ActiveProfile", QVariant::fromValue(QDBusVariant(profile)));
        if (!reply.isValid()) {
            qWarning().noquote() << "Failed to set power profile" << profile;
        }
        QGuiApplication::quit();
    }

private:
    QString m_currentProfile;

    static QString readCurrentProfile()
    {
        auto properties = propertiesInterface();
        const QDBusReply<QVariant> reply = properties.call("Get", interface, "ActiveProfile");
        if (!reply.isValid()) {
            qWarning().noquote() << "Failed to read current power profile";
            return "balanced";
        }

        const QString profile = reply.value().value<QDBusVariant>().variant().toString();
        return profile.isEmpty() ? QStringLiteral("balanced") : profile;
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
    height: 220
    visible: true
    title: "Power Profiles"
    color: "#00000000"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    readonly property var buttons: [performanceButton, balancedButton, powerSaverButton]
    readonly property string currentProfile: actions.currentProfile

    Component.onCompleted: {
        root.raise()
        root.requestActivate()

        if (root.currentProfile === "performance") {
            performanceButton.forceActiveFocus()
        } else if (root.currentProfile === "power-saver") {
            powerSaverButton.forceActiveFocus()
        } else {
            balancedButton.forceActiveFocus()
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
                id: performanceButton
                text: "Performance"
                selected: root.currentProfile === "performance"
                KeyNavigation.down: balancedButton
                KeyNavigation.up: powerSaverButton
                onClicked: actions.setProfile("performance")
            }

            MenuButton {
                id: balancedButton
                text: "Balanced"
                selected: root.currentProfile === "balanced"
                KeyNavigation.down: powerSaverButton
                KeyNavigation.up: performanceButton
                onClicked: actions.setProfile("balanced")
            }

            MenuButton {
                id: powerSaverButton
                text: "Power Saver"
                selected: root.currentProfile === "power-saver"
                KeyNavigation.down: performanceButton
                KeyNavigation.up: balancedButton
                onClicked: actions.setProfile("power-saver")
            }
        }
    }

    component MenuButton: Button {
        id: button
        property bool selected: false
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
    QGuiApplication::setApplicationName("power-menu");
    QGuiApplication::setDesktopFileName("power-menu");
    QQuickStyle::setStyle("Basic");

    ProfileActions actions;
    EscapeFilter escapeFilter;
    app.installEventFilter(&escapeFilter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("actions", &actions);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QGuiApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadData(qml, QUrl("qrc:/PowerProfiles.qml"));

    return app.exec();
}

#include "power-menu.moc"
