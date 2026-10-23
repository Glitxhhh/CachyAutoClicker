#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <chrono>

// ---------------------------------------------------------------------------------------------------------
// Knows when KDE's window switcher (Alt+Tab) is open, without needing to see the Alt key.
//
// The /dev/input reader only sees physical keyboards, and keys forwarded by Deskflow / Synergy / Input Leap go
// straight into the compositor, so Alt on such a keyboard is invisible to it. But KDE's shortcut daemon announces
// every global shortcut the compositor handles, whatever device it came from: "Walk Through Windows" is pressed when
// Alt+Tab is hit and released when Alt comes up, which brackets the switcher exactly (measured on Plasma 6: released
// 2.580 s after pressed for a 2.58 s Tab-down to Alt-up).
//
// active() is safe to call from any thread; the D-Bus slots run on the thread that created this object.
// ---------------------------------------------------------------------------------------------------------
class KdeSwitcherWatcher : public QObject {
    Q_OBJECT
public:
    explicit KdeSwitcherWatcher(QObject *parent = nullptr) : QObject(parent) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) return;   // no session bus: the /dev/input Alt tracking still works

        // Not KDE: nobody ever sends these, so this simply never fires.
        bus.connect("org.kde.kglobalaccel", "/component/kwin", "org.kde.kglobalaccel.Component",
                    "globalShortcutPressed", this, SLOT(onPressed(QString,QString,qlonglong)));
        bus.connect("org.kde.kglobalaccel", "/component/kwin", "org.kde.kglobalaccel.Component",
                    "globalShortcutReleased", this, SLOT(onReleased(QString,QString,qlonglong)));
    }

    // True while a switcher shortcut is held down.
    bool active() const {
        if (!flag.load()) return false;
        // If a "released" is ever missed (the daemon restarts mid-switch), don't stay paused forever.
        return nowMs() - since_ms.load() <= MAX_SWITCH_MS;
    }

private slots:
    void onPressed(const QString &component, const QString &shortcut, qlonglong timestamp) {
        Q_UNUSED(component);
        Q_UNUSED(timestamp);
        if (!isSwitcher(shortcut)) return;   // Walk Through Windows, ... (Reverse), ... of Current Application
        if (pressed.isEmpty()) since_ms = nowMs();
        pressed.insert(shortcut);
        flag = true;
    }

    void onReleased(const QString &component, const QString &shortcut, qlonglong timestamp) {
        Q_UNUSED(component);
        Q_UNUSED(timestamp);
        if (!isSwitcher(shortcut)) return;
        pressed.remove(shortcut);
        flag = !pressed.isEmpty();
    }

private:
    static constexpr long long MAX_SWITCH_MS = 20000;

    static bool isSwitcher(const QString &shortcut) { return shortcut.startsWith("Walk Through Windows"); }

    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    QSet<QString> pressed;   // only touched on the owning thread
    std::atomic<bool> flag{false};
    std::atomic<long long> since_ms{0};
};
