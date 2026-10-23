#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtDBus/QtDBus>

#include <functional>

// ---------------------------------------------------------------------------------------------------------
// Global hotkey through the XDG desktop portal (org.freedesktop.portal.GlobalShortcuts).
//
// The /dev/input reader only sees physical keyboards. Keys forwarded by Deskflow, Synergy or Input Leap on
// Wayland are injected into the compositor through libei and never appear as kernel input devices, so that reader
// can't see them. The compositor does see them, and this asks it to report the key.
// The first time, the desktop shows a one-time dialog to confirm the shortcut.
//
// An unsandboxed app must register an app id first, and the portal only accepts ids with a matching .desktop
// file, so installDesktopEntry() writes one for the current user.
// ---------------------------------------------------------------------------------------------------------

struct PortalShortcutInfo {
    QString id;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(PortalShortcutInfo)
Q_DECLARE_METATYPE(QList<PortalShortcutInfo>)

inline QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcutInfo &s) {
    arg.beginStructure();
    arg << s.id << s.properties;
    arg.endStructure();
    return arg;
}

inline const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcutInfo &s) {
    arg.beginStructure();
    arg >> s.id >> s.properties;
    arg.endStructure();
    return arg;
}

// One-shot receiver for a portal Request's Response signal.
class PortalResponse : public QObject {
    Q_OBJECT
public:
    std::function<void(uint, const QVariantMap &)> handler;
    explicit PortalResponse(QObject *parent) : QObject(parent) {}

public slots:
    void onResponse(uint code, const QVariantMap &results) {
        auto h = handler;
        deleteLater();
        if (h) h(code, results);
    }
};

class PortalShortcut : public QObject {
    Q_OBJECT
public:
    static constexpr const char *APP_ID = "org.glitxh.CachyAutoClicker";

    explicit PortalShortcut(QObject *parent = nullptr) : QObject(parent) {
        qDBusRegisterMetaType<PortalShortcutInfo>();
        qDBusRegisterMetaType<QList<PortalShortcutInfo>>();
    }

    // The key as the XDG shortcuts spec writes it (xkb names), or empty to let the user pick in the dialog.
    static QString triggerFor(int qt_key) {
        if (qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F24) return QString("F%1").arg(qt_key - Qt::Key_F1 + 1);
        if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) return QString(QChar('a' + (qt_key - Qt::Key_A)));
        if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) return QString(QChar('0' + (qt_key - Qt::Key_0)));
        if (qt_key == Qt::Key_Space) return "space";
        return QString();
    }

    // Changes the key the desktop has bound, for when the user picks a new hotkey in the app. The portal can't do
    // this (once a shortcut id is known the desktop keeps the user's key and ignores our preference), so on KDE this
    // makes the same call System Settings uses to change another app's shortcut: no dialog, no editor.
    void rebind(int qt_key, const QString &trigger_name) {
        if (!bound) return;
        QDBusMessage msg = QDBusMessage::createMethodCall("org.kde.kglobalaccel", "/kglobalaccel",
                                                          "org.kde.KGlobalAccel", "setForeignShortcut");
        // actionId = [component, action, component's display name, action's display name]
        msg << QStringList{QString(APP_ID), "toggle", "Cachy AutoClicker", "Start or stop the clicker"}
            << QVariant::fromValue(QList<int>{qt_key});
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, trigger_name]() {
            if (watcher->isError())
                setStatus("The desktop shortcut keeps its old key (this desktop can't be changed from the app). Change it in your desktop's shortcut settings.");
            else
                setStatus(QString("Also bound through your desktop's global shortcuts: %1 (change it in System Settings > Shortcuts)").arg(trigger_name));
            watcher->deleteLater();
        });
    }

    void start(const QString &preferred_trigger) {
        preferred = preferred_trigger;
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            setStatus("No desktop session bus, so only keyboards the app can read directly work for the hotkey.");
            return;
        }

        installDesktopEntry();

        // Must be the first portal call on this connection, and only works for ids that have a .desktop file.
        QDBusMessage reg = QDBusMessage::createMethodCall(PORTAL, PORTAL_PATH, "org.freedesktop.host.portal.Registry", "Register");
        reg << QString(APP_ID) << QVariantMap();
        QDBusMessage reply = bus.call(reg, QDBus::Block, 5000);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setStatus(QString("Global shortcut unavailable (%1). Only keyboards the app can read directly work for the hotkey.")
                          .arg(reply.errorMessage()));
            return;
        }

        bus.connect(QString(), PORTAL_PATH, GS_IFACE, "Activated", this,
                    SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));

        setStatus("Asking the desktop for a shortcut session...");
        callRequest("CreateSession",
            [](QDBusMessage &msg, const QString &token) {
                QVariantMap options;
                options["handle_token"] = token;
                options["session_handle_token"] = "cac" + token;
                msg << options;
            },
            [this](uint code, const QVariantMap &results) {
                if (code != 0) {
                    setStatus("The desktop refused to start a shortcut session.");
                    return;
                }
                session_path = results.value("session_handle").toString();
                bind();
            });
    }

signals:
    void activated();
    void statusChanged(const QString &status);

private slots:
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options) {
        Q_UNUSED(timestamp);
        Q_UNUSED(options);
        if (id == "toggle" && session.path() == session_path) emit activated();
    }

private:
    static constexpr const char *PORTAL = "org.freedesktop.portal.Desktop";
    static constexpr const char *PORTAL_PATH = "/org/freedesktop/portal/desktop";
    static constexpr const char *GS_IFACE = "org.freedesktop.portal.GlobalShortcuts";

    QString preferred;
    bool bound = false;
    QString session_path;
    int token_counter = 0;

    void setStatus(const QString &text) { emit statusChanged(text); }

    void bind() {
        setStatus("Waiting for you to confirm the shortcut in the dialog your desktop shows (first time only)...");
        callRequest("BindShortcuts",
            [this](QDBusMessage &msg, const QString &token) {
                PortalShortcutInfo shortcut;
                shortcut.id = "toggle";
                shortcut.properties["description"] = "Start or stop the clicker";
                if (!preferred.isEmpty()) shortcut.properties["preferred_trigger"] = preferred;

                QVariantMap options;
                options["handle_token"] = token;
                msg << QVariant::fromValue(QDBusObjectPath(session_path))
                    << QVariant::fromValue(QList<PortalShortcutInfo>{shortcut})
                    << QString() << options;
            },
            [this](uint code, const QVariantMap &results) {
                if (code != 0) {
                    setStatus("The shortcut wasn't confirmed, so the global hotkey only works from keyboards the app can read directly.");
                    return;
                }
                bound = true;
                QString trigger;
                const QVariant shortcuts = results.value("shortcuts");
                if (shortcuts.canConvert<QDBusArgument>()) {
                    QList<PortalShortcutInfo> list;
                    shortcuts.value<QDBusArgument>() >> list;
                    if (!list.isEmpty()) trigger = list.first().properties.value("trigger_description").toString();
                }
                if (trigger.isEmpty() && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("Hyprland", Qt::CaseInsensitive))
                    setStatus(QString("Registered with Hyprland. Add this line to hyprland.conf to assign a key:  bind = , F6, global, %1:toggle").arg(APP_ID));
                else if (trigger.isEmpty())
                    setStatus("Registered with your desktop, but no key is assigned. Set one in your desktop's shortcut settings (KDE: System Settings > Shortcuts > Cachy AutoClicker).");
                else
                    setStatus(QString("Also bound through your desktop's global shortcuts: %1 (change it in System Settings > Shortcuts)").arg(trigger));
            });
    }

    // Calls a GlobalShortcuts method that answers through a Request object's Response signal. The listener is
    // installed before the call so a fast answer can't be missed.
    void callRequest(const QString &method,
                     const std::function<void(QDBusMessage &, const QString &)> &fill,
                     std::function<void(uint, const QVariantMap &)> done) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const QString token = QString("cac%1_%2").arg(QCoreApplication::applicationPid()).arg(++token_counter);
        // The portal answers on .../request/<our unique name with ':' dropped and '.' -> '_'>/<token>
        const QString sender = bus.baseService().mid(1).replace('.', '_');
        const QString request_path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;

        auto *waiter = new PortalResponse(this);
        waiter->handler = std::move(done);
        bus.connect(QString(), request_path, "org.freedesktop.portal.Request", "Response", waiter,
                    SLOT(onResponse(uint,QVariantMap)));

        QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL, PORTAL_PATH, GS_IFACE, method);
        fill(msg, token);

        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, waiter]() {
            if (watcher->isError()) {
                setStatus(QString("Global shortcut unavailable (%1). Only keyboards the app can read directly work for the hotkey.")
                              .arg(watcher->error().message()));
                waiter->deleteLater();
            }
            watcher->deleteLater();
        });
    }

    // The portal only accepts app ids that have a matching .desktop file, so write one for the current user
    // (no root needed). It also gives the app a launcher entry and the name the desktop shows in its dialog.
    static void installDesktopEntry() {
        const QString data_home = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + "/.local/share");
        const QString icon_dir = data_home + "/CachyAutoClicker";
        const QString icon_path = icon_dir + "/icon.png";
        QDir().mkpath(icon_dir);
        if (!QFile::exists(icon_path) && QFile::copy(":/icon.png", icon_path))
            QFile::setPermissions(icon_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                 QFileDevice::ReadGroup | QFileDevice::ReadOther);

        // Exec key: wrap in quotes and escape the characters the desktop entry spec treats specially.
        QString exec = "\"";
        const QString exe = QCoreApplication::applicationFilePath();
        for (const QChar c : exe) {
            if (c == '"' || c == '`' || c == '$' || c == '\\') exec += '\\';
            if (c == '%') exec += '%';
            exec += c;
        }
        exec += '"';

        const QString content =
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Cachy AutoClicker\n"
            "Comment=Auto-clicker and key holder\n"
            "Exec=" + exec + "\n"
            "Icon=" + icon_path + "\n"
            "Terminal=false\n"
            "Categories=Utility;\n";

        const QString apps = data_home + "/applications";
        QDir().mkpath(apps);
        QFile file(apps + "/" + QString(APP_ID) + ".desktop");
        if (file.open(QIODevice::ReadOnly) && file.readAll() == content.toUtf8()) return;   // already current
        file.close();
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(content.toUtf8());   // rewritten when the app moves
    }
};
