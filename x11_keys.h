#pragma once

#include <functional>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------------------------------------
// Global hotkey and Alt tracking for X11 sessions (GNOME on X11, XFCE, Cinnamon, MATE, i3, KDE on X11, ...).
//
// Deskflow / Synergy / Input Leap inject keys into an X11 session through XTest, so they never appear in
// /dev/input. The X server does see them, so a passive key grab (XGrabKey) catches the hotkey, and XQueryKeymap
// reports whether Alt is down, for any keyboard.
//
// Kept free of Qt and Xlib types on purpose: Xlib's macros (None, Bool, KeyPress...) clash with Qt's headers, so all
// of the Xlib code lives in x11_keys.cpp. Without libX11 at build time every call is a harmless no-op.
// ---------------------------------------------------------------------------------------------------------
class X11Keys {
public:
    X11Keys();
    ~X11Keys();

    // Opens the display and grabs the key (a Qt::Key value). on_pressed runs on a background thread.
    // Returns false if no X server is reachable or this build has no X11 support.
    bool start(int qt_key, std::function<void()> on_pressed);

    // Moves the grab to another key (the user picked a new hotkey).
    void setHotkey(int qt_key);

    // True while Alt is down on any keyboard the X server knows about. Cheap to call often.
    bool altHeld();

    // Text for the UI ("Also grabbed through X11: F6 ...", or why it couldn't be).
    std::string status();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
