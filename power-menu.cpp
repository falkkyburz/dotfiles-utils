#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtLogging>

namespace {
constexpr auto service = "net.hadess.PowerProfiles";
constexpr auto path = "/net/hadess/PowerProfiles";
constexpr auto interface = "net.hadess.PowerProfiles";

QDBusInterface powerProfilesInterface()
{
    return QDBusInterface(service, path, interface, QDBusConnection::systemBus());
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
        auto powerProfiles = powerProfilesInterface();
        if (!powerProfiles.setProperty("ActiveProfile", profile)) {
            qWarning().noquote() << "Failed to set power profile" << profile << powerProfiles.lastError().message();
        }
        QGuiApplication::quit();
    }

private:
    QString m_currentProfile;

    static QString readCurrentProfile()
    {
        auto powerProfiles = powerProfilesInterface();
        const QString profile = powerProfiles.property("ActiveProfile").toString();
        if (!powerProfiles.lastError().isValid() && !profile.isEmpty()) {
            return profile;
        }

        if (powerProfiles.lastError().isValid()) {
            qWarning().noquote() << "Failed to read current power profile" << powerProfiles.lastError().message();
            return "balanced";
        }

        return QStringLiteral("balanced");
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
