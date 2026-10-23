#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>
#include <QKeyEvent>
#include <QKeySequence>
#include <QCloseEvent>
#include <QIcon>
#include <QFile>
#include <QElapsedTimer>
#include <QSettings>

#include "portal_shortcut.h"
#include "command_channel.h"

#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <cstring>
#include <algorithm>
#include <atomic>

// Complete explicit map of Qt Keys to Linux Kernel Evdev Keycodes
int qt_key_to_linux_keycode(int qt_key) {
    switch(qt_key) {
        case Qt::Key_A: return KEY_A; case Qt::Key_B: return KEY_B; case Qt::Key_C: return KEY_C;
        case Qt::Key_D: return KEY_D; case Qt::Key_E: return KEY_E; case Qt::Key_F: return KEY_F;
        case Qt::Key_G: return KEY_G; case Qt::Key_H: return KEY_H; case Qt::Key_I: return KEY_I;
        case Qt::Key_J: return KEY_J; case Qt::Key_K: return KEY_K; case Qt::Key_L: return KEY_L;
        case Qt::Key_M: return KEY_M; case Qt::Key_N: return KEY_N; case Qt::Key_O: return KEY_O;
        case Qt::Key_P: return KEY_P; case Qt::Key_Q: return KEY_Q; case Qt::Key_R: return KEY_R;
        case Qt::Key_S: return KEY_S; case Qt::Key_T: return KEY_T; case Qt::Key_U: return KEY_U;
        case Qt::Key_V: return KEY_V; case Qt::Key_W: return KEY_W; case Qt::Key_X: return KEY_X;
        case Qt::Key_Y: return KEY_Y; case Qt::Key_Z: return KEY_Z;
        case Qt::Key_1: return KEY_1; case Qt::Key_2: return KEY_2; case Qt::Key_3: return KEY_3;
        case Qt::Key_4: return KEY_4; case Qt::Key_5: return KEY_5; case Qt::Key_6: return KEY_6;
        case Qt::Key_7: return KEY_7; case Qt::Key_8: return KEY_8; case Qt::Key_9: return KEY_9;
        case Qt::Key_0: return KEY_0;
        case Qt::Key_Space: return KEY_SPACE; case Qt::Key_Return: return KEY_ENTER;
        case Qt::Key_Shift: return KEY_LEFTSHIFT; case Qt::Key_Control: return KEY_LEFTCTRL;
        case Qt::Key_Alt: return KEY_LEFTALT; case Qt::Key_Tab: return KEY_TAB;
        case Qt::Key_Escape: return KEY_ESC; case Qt::Key_Backspace: return KEY_BACKSPACE;
        case Qt::Key_F1: return KEY_F1; case Qt::Key_F2: return KEY_F2; case Qt::Key_F3: return KEY_F3;
        case Qt::Key_F4: return KEY_F4; case Qt::Key_F5: return KEY_F5; case Qt::Key_F6: return KEY_F6;
        case Qt::Key_F7: return KEY_F7; case Qt::Key_F8: return KEY_F8; case Qt::Key_F9: return KEY_F9;
        case Qt::Key_F10: return KEY_F10; case Qt::Key_F11: return KEY_F11; case Qt::Key_F12: return KEY_F12;
        default: return KEY_E;
    }
}

// True when the Qt key has a real Linux keycode in the map above (the map falls back to E for everything else).
static bool qt_key_is_mapped(int qt_key) {
    return qt_key == Qt::Key_E || qt_key_to_linux_keycode(qt_key) != KEY_E;
}

// Special text field that handles locking focus and custom aesthetic states
class FrequencyLineEdit : public QLineEdit {
    Q_OBJECT
public:
    FrequencyLineEdit(const QString &contents, QWidget *parent = nullptr) : QLineEdit(contents, parent) {
        // Enforce the CachyOS dark theme / interactive states aesthetic via stylesheet
        setStyleSheet(
            "QLineEdit {"
            "    background-color: #23242c;"
            "    color: #ffffff;"
            "    border: 1px solid #45475a;"
            "    border-radius: 4px;"
            "    padding: 4px;"
            "    font-weight: bold;"
            "}"
            "QLineEdit:hover {"
            "    border: 1px solid #ff5722;" /* Light hint that it can be interacted with */
            "}"
            "QLineEdit:focus {"
            "    background-color: #2a2b36;"
            "    border: 2px solid #00ffcc;" /* Light up bright cyan glow when focused/active */
            "}"
            "QLineEdit:disabled {"
            "    background-color: #16161a;"
            "    color: #585b70;"
            "    border: 1px solid #2a2b36;"
            "}"
        );
        
        // Lock out text editing when user presses Enter
        connect(this, &QLineEdit::returnPressed, this, &QWidget::clearFocus);
    }

protected:
    // Lock out text editing when user clicks away anywhere else
    void focusOutEvent(QFocusEvent *event) override {
        QLineEdit::focusOutEvent(event);
        clearFocus();
    }
};

// Interactive button that intercepts hardware keys natively
class KeySelectorButton : public QPushButton {
    Q_OBJECT
private:
    bool listening = false;
    int current_linux_code;
    int current_qt_key;
    QString current_key_name;
    bool only_mapped_keys;

public:
    // only_mapped: ignore keys that have no known Linux keycode (used for the hotkey, where silently falling
    // back to "E" would be wrong).
    KeySelectorButton(QWidget *parent = nullptr, int qt_key = Qt::Key_E, bool only_mapped = false)
        : QPushButton(QKeySequence(qt_key).toString(), parent),
          current_linux_code(qt_key_to_linux_keycode(qt_key)), current_qt_key(qt_key),
          current_key_name(QKeySequence(qt_key).toString()), only_mapped_keys(only_mapped) {
        setCheckable(true);
        setStyleSheet(
            "QPushButton {"
            "    background-color: #2a2b36;"
            "    color: #ffffff;"
            "    border: 1px solid #45475a;"
            "    border-radius: 4px;"
            "    padding: 6px;"
            "}"
            "QPushButton:checked {"
            "    background-color: #ff5722;"
            "    color: white;"
            "    font-weight: bold;"
            "}"
        );
        connect(this, &QPushButton::clicked, this, &KeySelectorButton::startListening);
    }

    int getLinuxCode() const { return current_linux_code; }
    int getQtKey() const { return current_qt_key; }
    QString getKeyName() const { return current_key_name; }

signals:
    void keyChanged(int qt_key, int linux_code, const QString &name);

private:
    void cancelListening() {
        if (!listening) return;
        listening = false;
        releaseKeyboard();
        setChecked(false);
        setText(current_key_name);
    }

    void startListening() {
        listening = true;
        setText("Press any key...");
        grabKeyboard();
    }

protected:
    // Clicking away while it's waiting for a key cancels, instead of leaving the keyboard grabbed.
    void focusOutEvent(QFocusEvent *event) override {
        QPushButton::focusOutEvent(event);
        cancelListening();
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (listening) {
            int k = event->key();
            if (k == Qt::Key_Escape || (only_mapped_keys && !qt_key_is_mapped(k))) {
                cancelListening();
                return;
            }

            listening = false;
            releaseKeyboard();
            setChecked(false);

            current_linux_code = qt_key_to_linux_keycode(k);
            current_qt_key = k;
            current_key_name = QKeySequence(k).toString();
            if (current_key_name.isEmpty()) current_key_name = QString("Key %1").arg(k);

            setText(current_key_name);
            emit keyChanged(k, current_linux_code, current_key_name);
        } else {
            QPushButton::keyPressEvent(event);
        }
    }
};

// Global Hotkey Listener Thread
class HotkeyWorker : public QThread {
    Q_OBJECT
signals:
    void hotkeyPressed();
public:
    std::atomic<int> hotkey_code{KEY_F6};
    bool running = true;

protected:
    void run() override {
        std::vector<int> fds;
        for (int i = 0; i < 32; ++i) {
            std::string path = "/dev/input/event" + std::to_string(i);
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) fds.push_back(fd);
        }

        std::vector<struct pollfd> pfds(fds.size());
        for (size_t i = 0; i < fds.size(); ++i) {
            pfds[i].fd = fds[i];
            pfds[i].events = POLLIN;
        }

        while (running) {
            int ret = poll(pfds.data(), pfds.size(), 100);
            if (ret > 0) {
                for (size_t i = 0; i < pfds.size(); ++i) {
                    if (pfds[i].revents & POLLIN) {
                        struct input_event ev;
                        while (read(pfds[i].fd, &ev, sizeof(ev)) > 0) {
                            if (ev.type == EV_KEY && ev.code == hotkey_code && ev.value == 1) {
                                emit hotkeyPressed();
                            }
                        }
                    }
                }
            }
        }
        for (int fd : fds) close(fd);
    }
};

// Autoclick / Hold Execution Driver Thread
class ClickerWorker : public QThread {
    Q_OBJECT
public:
    std::string mode;
    int target_code;
    int interval_ms;
    bool running = true;

    void stop() { running = false; }

protected:
    void run() override {
        int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) return;

        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_EVBIT, EV_SYN);

        for (int i = 1; i < 255; ++i) ioctl(fd, UI_SET_KEYBIT, i);
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
        ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);

        struct uinput_setup usetup;
        std::memset(&usetup, 0, sizeof(usetup));
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor = 0x1234;
        usetup.id.product = 0x5678;
        std::strcpy(usetup.name, "CachyOS Virtual Input");

        ioctl(fd, UI_DEV_SETUP, &usetup);
        ioctl(fd, UI_DEV_CREATE);
        usleep(100000);

        if (mode == "Hold") {
            send_event(fd, EV_KEY, target_code, 1);
            send_event(fd, EV_SYN, SYN_REPORT, 0);
            while (running) { usleep(10000); }
            send_event(fd, EV_KEY, target_code, 0);
            send_event(fd, EV_SYN, SYN_REPORT, 0);
        } else {
            while (running) {
                send_event(fd, EV_KEY, target_code, 1);
                send_event(fd, EV_SYN, SYN_REPORT, 0);
                usleep(2000); // physical press cycle duration
                send_event(fd, EV_KEY, target_code, 0);
                send_event(fd, EV_SYN, SYN_REPORT, 0);

                int sleep_time = (interval_ms * 1000) - 2000;
                if (sleep_time > 0) usleep(sleep_time);
            }
        }

        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }

private:
    void send_event(int fd, int type, int code, int val) {
        struct input_event ie;
        std::memset(&ie, 0, sizeof(ie));
        ie.type = type;
        ie.code = code;
        ie.value = val;
        write(fd, &ie, sizeof(ie));
    }
};

class MainWindow : public QWidget {
    Q_OBJECT
private:
    QRadioButton *btn_autoclick, *btn_hold;
    QRadioButton *btn_mouse, *btn_keyboard;
    QComboBox *mouse_combo;
    KeySelectorButton *key_selector;
    FrequencyLineEdit *interval_input;
    QLabel *interval_label;
    QLabel *status_label;
    
    QLabel *portal_label;
    KeySelectorButton *hotkey_selector;
    QString hotkey_name = "F6";

    ClickerWorker *worker = nullptr;
    HotkeyWorker *hotkey_listener = nullptr;
    PortalShortcut *portal = nullptr;
    CommandServer *commands = nullptr;
    bool is_running = false;

    // The /dev/input hotkey, the desktop shortcut and a --toggle command can all fire for one key press (a physical
    // keyboard is seen by both the reader and the portal), so toggles this close together count as one.
    static constexpr qint64 SHORTCUT_DEBOUNCE_MS = 800;
    QElapsedTimer since_last_shortcut;

public:
    MainWindow() {
        setWindowTitle("Key Clicker Holder");
        resize(340, 420);

        // Clicking empty space gives the window focus, so a text box (or the key selector) lets go of it.
        // Without this a QLineEdit keeps focus until another focusable widget is clicked.
        setFocusPolicy(Qt::ClickFocus);

        // Apply dark baseline container style matching target image
        setStyleSheet("QWidget { background-color: #1e1e24; color: #ffffff; font-family: 'Segoe UI', sans-serif; }");

        QVBoxLayout *layout = new QVBoxLayout(this);

        // Action Group
        layout->addWidget(new QLabel("<b>Action</b>", this));
        QHBoxLayout *action_layout = new QHBoxLayout();
        btn_autoclick = new QRadioButton("Autoclick", this);
        btn_hold = new QRadioButton("Hold", this);
        btn_autoclick->setChecked(true);
        QButtonGroup *action_group = new QButtonGroup(this);
        action_group->addButton(btn_autoclick);
        action_group->addButton(btn_hold);
        action_layout->addWidget(btn_autoclick);
        action_layout->addWidget(btn_hold);
        layout->addLayout(action_layout);

        // Device Group
        layout->addWidget(new QLabel("<b>Device</b>", this));
        QHBoxLayout *device_layout = new QHBoxLayout();
        btn_mouse = new QRadioButton("Mouse", this);
        btn_keyboard = new QRadioButton("Keyboard", this);
        btn_mouse->setChecked(true);
        QButtonGroup *device_group = new QButtonGroup(this);
        device_group->addButton(btn_mouse);
        device_group->addButton(btn_keyboard);
        device_layout->addWidget(btn_mouse);
        device_layout->addWidget(btn_keyboard);
        layout->addLayout(device_layout);

        // Target Key/Button Selector
        layout->addWidget(new QLabel("<b>Key / Button Selection</b>", this));
        mouse_combo = new QComboBox(this);
        mouse_combo->addItems({"Left Click", "Right Click", "Middle Click"});
        mouse_combo->setStyleSheet("QComboBox { background-color: #2a2b36; color: white; padding: 5px; border: 1px solid #45475a; border-radius: 4px; }");
        
        key_selector = new KeySelectorButton(this);
        key_selector->hide();
        layout->addWidget(mouse_combo);
        layout->addWidget(key_selector);

        // Frequency Settings layout
        layout->addWidget(new QLabel("<b>Autoclick controls</b>", this));
        QHBoxLayout *freq_layout = new QHBoxLayout();
        interval_label = new QLabel("Autoclick frequency (ms):", this);
        interval_input = new FrequencyLineEdit("100", this);
        freq_layout->addWidget(interval_label);
        freq_layout->addWidget(interval_input);
        layout->addLayout(freq_layout);

        // Toggle hotkey (saved between launches)
        layout->addWidget(new QLabel("<b>Toggle hotkey</b>", this));
        QSettings settings("CachyAutoClicker", "CachyAutoClicker");
        int saved_hotkey = settings.value("hotkey_qt_key", int(Qt::Key_F6)).toInt();
        if (!qt_key_is_mapped(saved_hotkey)) saved_hotkey = Qt::Key_F6;
        hotkey_selector = new KeySelectorButton(this, saved_hotkey, true);
        hotkey_name = hotkey_selector->getKeyName();
        layout->addWidget(hotkey_selector);

        layout->addSpacerItem(new QSpacerItem(20, 20, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding));

        // Global Status Indicator
        status_label = new QLabel(statusText(), this);
        layout->addWidget(status_label);

        portal_label = new QLabel("", this);
        portal_label->setWordWrap(true);
        portal_label->setStyleSheet("QLabel { color: #8e92b5; font-size: 11px; }");
        portal_label->hide();
        layout->addWidget(portal_label);

        // Event Hookups
        connect(btn_mouse, &QRadioButton::toggled, this, &MainWindow::updateDeviceUI);
        connect(btn_autoclick, &QRadioButton::toggled, this, &MainWindow::updateActionUI);

        // Bind Window Icon cleanly checking working directory paths dynamically
        loadWindowIcon();

        // Start global thread tracking
        hotkey_listener = new HotkeyWorker();
        hotkey_listener->hotkey_code = hotkey_selector->getLinuxCode();
        connect(hotkey_listener, &HotkeyWorker::hotkeyPressed, this, &MainWindow::toggleFromShortcut);
        hotkey_listener->start();

        // Also ask the desktop for the key: that path sees keys forwarded by Deskflow & co, which never reach /dev/input.
        portal = new PortalShortcut(this);
        connect(portal, &PortalShortcut::activated, this, &MainWindow::toggleFromShortcut);
        connect(portal, &PortalShortcut::statusChanged, this, [this](const QString &text) {
            std::fprintf(stderr, "[portal] %s\n", text.toUtf8().constData());
            portal_label->setText(text);
            portal_label->setVisible(!text.isEmpty());
        });
        portal->start(PortalShortcut::triggerFor(hotkey_selector->getQtKey()));
        connect(hotkey_selector, &KeySelectorButton::keyChanged, this, &MainWindow::onHotkeyChanged);

        // "CachyAutoClicker --toggle|--start|--stop|--status" from another process.
        commands = new CommandServer([this](const QString &command) {
            if (command == "toggle") toggleFromShortcut();
            else if (command == "start" && !is_running) toggleEngine();
            else if (command == "stop" && is_running) toggleEngine();
            return QString(is_running ? "running" : "stopped");
        }, this);
    }

private:
    void loadWindowIcon() {
        setWindowIcon(QIcon(":/icon.png"));
    }

    QString statusText() const {
        return QString("<center><font color='%1'><b>Status: %2</b></font><br>Press <b>%3</b> globally to start or stop</center>")
            .arg(is_running ? "#2ecc71" : "#e74c3c", is_running ? "RUNNING" : "STOPPED", hotkey_name);
    }

private slots:
    // The user picked a new toggle key: use it for the /dev/input reader, remember it, and move the desktop's
    // binding too (KDE; other desktops keep theirs and the status line says so).
    void onHotkeyChanged(int qt_key, int linux_code, const QString &name) {
        hotkey_listener->hotkey_code = linux_code;
        hotkey_name = name;
        status_label->setText(statusText());
        QSettings("CachyAutoClicker", "CachyAutoClicker").setValue("hotkey_qt_key", qt_key);
        portal->rebind(qt_key, name);
    }

    void updateDeviceUI() {
        if (btn_mouse->isChecked()) {
            mouse_combo->show();
            key_selector->hide();
        } else {
            mouse_combo->hide();
            key_selector->show();
        }
    }

    void updateActionUI(bool checked) {
        // Toggle text box availability based on "Autoclick" selection status
        interval_input->setEnabled(checked);
        interval_label->setEnabled(checked);
    }

    void toggleFromShortcut() {
        if (since_last_shortcut.isValid() && since_last_shortcut.elapsed() < SHORTCUT_DEBOUNCE_MS) return;
        since_last_shortcut.start();
        toggleEngine();
    }

    void toggleEngine() {
        if (is_running) {
            if (worker) {
                worker->stop();
                worker->wait();
                delete worker;
                worker = nullptr;
            }
            is_running = false;
            status_label->setText(statusText());
        } else {
            worker = new ClickerWorker();
            worker->mode = btn_hold->isChecked() ? "Hold" : "Autoclick";
            worker->interval_ms = interval_input->text().toInt();
            if (worker->interval_ms <= 0) worker->interval_ms = 100; // Safe fallback validation

            if (btn_mouse->isChecked()) {
                QString click_type = mouse_combo->currentText();
                if (click_type == "Right Click") worker->target_code = BTN_RIGHT;
                else if (click_type == "Middle Click") worker->target_code = BTN_MIDDLE;
                else worker->target_code = BTN_LEFT;
            } else {
                worker->target_code = key_selector->getLinuxCode();
            }

            is_running = true;
            status_label->setText(statusText());
            worker->start();
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        if (worker) { worker->stop(); worker->wait(); }
        if (hotkey_listener) { hotkey_listener->running = false; hotkey_listener->wait(); }
        event->accept();
    }
};

int main(int argc, char *argv[]) {
    // "CachyAutoClicker --toggle" etc.: talk to the copy that's already running, then exit (no window).
    if (argc == 2 && command_channel::isCommand(argv[1])) {
        QCoreApplication client(argc, argv);
        return command_channel::runClient(argv[1]);
    }

    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"