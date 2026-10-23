#include "x11_keys.h"

#ifdef CAC_HAVE_X11

#include <X11/Xlib.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
// XGrabKey must be repeated for each Caps/Num Lock state, or the key stops working while a lock is on.
const unsigned int LOCK_COMBOS[4] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};

std::atomic<bool> g_grab_denied{false};

// The default X error handler exits the process; a failed grab (key already taken) must not.
int xError(Display *, XErrorEvent *e) {
    if (e->error_code == BadAccess) g_grab_denied = true;
    return 0;
}

// Qt::Key -> X11 keysym (0 = not mapped). Qt's values are written out so this file needs no Qt headers.
unsigned long keysymFor(int qt) {
    if (qt >= 0x01000030 && qt <= 0x01000047) return 0xFFBE + (qt - 0x01000030);   // F1..F24
    if (qt >= 0x41 && qt <= 0x5A) return 0x61 + (qt - 0x41);                       // A..Z (lowercase keysym)
    if (qt >= 0x30 && qt <= 0x39) return qt;                                       // 0..9
    switch (qt) {
        case 0x20: return 0x20;               // Space
        case 0x01000004: case 0x01000005: return 0xFF0D;   // Return / Enter
        case 0x01000000: return 0xFF1B;       // Escape
        case 0x01000001: return 0xFF09;       // Tab
        case 0x01000010: return 0xFF50;       // Home
        case 0x01000012: return 0xFF51;       // Left
        case 0x01000013: return 0xFF52;       // Up
        case 0x01000014: return 0xFF53;       // Right
        case 0x01000015: return 0xFF54;       // Down
        case 0x01000016: return 0xFF55;       // Page Up
        case 0x01000017: return 0xFF56;       // Page Down
        case 0x01000011: return 0xFF57;       // End
        case 0x01000006: return 0xFF63;       // Insert
        case 0x01000007: return 0xFFFF;       // Delete
        default: return 0;
    }
}

std::string keyName(int qt) {
    if (qt >= 0x01000030 && qt <= 0x01000047) return "F" + std::to_string(qt - 0x01000030 + 1);
    if ((qt >= 0x41 && qt <= 0x5A) || (qt >= 0x30 && qt <= 0x39)) return std::string(1, static_cast<char>(qt));
    return "the key";
}
}  // namespace

struct X11Keys::Impl {
    Display *grab_dpy = nullptr;    // used by the event thread (and by setHotkey under the mutex)
    Display *query_dpy = nullptr;   // used by altHeld
    std::thread thread;
    std::atomic<bool> stop{false};
    std::mutex mtx;
    std::function<void()> on_pressed;
    int keycode = 0;
    int alt_l = 0, alt_r = 0;
    std::chrono::steady_clock::time_point alt_checked{};
    bool alt_cached = false;
    std::string status_text;

    static bool isDown(const char keys[32], int keycode) {
        return keycode > 0 && (keys[keycode / 8] & (1 << (keycode % 8))) != 0;
    }

    void applyHotkey(int qt_key) {
        Window root = DefaultRootWindow(grab_dpy);
        if (keycode != 0)
            for (unsigned int combo : LOCK_COMBOS) XUngrabKey(grab_dpy, keycode, combo, root);
        keycode = 0;

        const unsigned long keysym = keysymFor(qt_key);
        const int kc = keysym ? XKeysymToKeycode(grab_dpy, keysym) : 0;
        if (kc == 0) {
            status_text = keyName(qt_key) + " can't be used as an X11 hotkey.";
            return;
        }

        g_grab_denied = false;
        for (unsigned int combo : LOCK_COMBOS) XGrabKey(grab_dpy, kc, combo, root, False, GrabModeAsync, GrabModeAsync);
        XSync(grab_dpy, False);   // surfaces a BadAccess right here instead of later

        if (g_grab_denied) {
            status_text = keyName(qt_key) + " is already grabbed by another program, so the hotkey only works from keyboards the app can read directly.";
            return;
        }
        keycode = kc;
        status_text = "Also grabbed through X11: " + keyName(qt_key) + " (works for keyboards forwarded by Deskflow and similar)";
    }

    void loop() {
        while (!stop) {
            bool any = false;
            {
                std::lock_guard<std::mutex> lock(mtx);
                while (XPending(grab_dpy) > 0) {
                    any = true;
                    XEvent ev;
                    XNextEvent(grab_dpy, &ev);
                    if (ev.type == KeyPress && on_pressed) on_pressed();
                }
            }
            if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

X11Keys::X11Keys() : d(new Impl) {}

X11Keys::~X11Keys() {
    d->stop = true;
    if (d->thread.joinable()) d->thread.join();
    if (d->grab_dpy) XCloseDisplay(d->grab_dpy);
    if (d->query_dpy) XCloseDisplay(d->query_dpy);
}

bool X11Keys::start(int qt_key, std::function<void()> on_pressed) {
    XSetErrorHandler(xError);
    d->query_dpy = XOpenDisplay(nullptr);
    d->grab_dpy = XOpenDisplay(nullptr);
    if (!d->query_dpy || !d->grab_dpy) return false;

    d->alt_l = XKeysymToKeycode(d->query_dpy, 0xFFE9);   // Alt_L
    d->alt_r = XKeysymToKeycode(d->query_dpy, 0xFFEA);   // Alt_R
    d->on_pressed = std::move(on_pressed);
    d->applyHotkey(qt_key);
    d->thread = std::thread([this]() { d->loop(); });
    return true;
}

void X11Keys::setHotkey(int qt_key) {
    if (!d->grab_dpy) return;
    std::lock_guard<std::mutex> lock(d->mtx);
    d->applyHotkey(qt_key);
}

bool X11Keys::altHeld() {
    if (!d->query_dpy) return false;
    std::lock_guard<std::mutex> lock(d->mtx);
    const auto now = std::chrono::steady_clock::now();
    if (now - d->alt_checked < std::chrono::milliseconds(10)) return d->alt_cached;   // don't hammer the server
    d->alt_checked = now;

    char keys[32];
    std::memset(keys, 0, sizeof(keys));
    XQueryKeymap(d->query_dpy, keys);
    d->alt_cached = Impl::isDown(keys, d->alt_l) || Impl::isDown(keys, d->alt_r);
    return d->alt_cached;
}

std::string X11Keys::status() {
    std::lock_guard<std::mutex> lock(d->mtx);
    return d->status_text;
}

#else  // built without libX11: everything is a no-op, the Wayland / evdev paths still work

struct X11Keys::Impl {};
X11Keys::X11Keys() : d(new Impl) {}
X11Keys::~X11Keys() = default;
bool X11Keys::start(int, std::function<void()>) { return false; }
void X11Keys::setHotkey(int) {}
bool X11Keys::altHeld() { return false; }
std::string X11Keys::status() { return {}; }

#endif
