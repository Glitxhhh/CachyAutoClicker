# CachyOS / Wayland Native Key Clicker & Holder

A native C++ Qt6 auto-clicker and key-holder utility designed specifically to bypass security restrictions on modern Wayland sessions (like CachyOS, Arch, or Fedora) using the Linux kernel `uinput` architecture.

![Icon](icon.png)

---

## ⚠️ CRITICAL: Permission Requirements (Read Before Running)

Because this tool interacts directly with kernel-level device emulation (`/dev/uinput`) to generate clicks and monitors a global hotkey via `/dev/input/` events, **it will not work out-of-the-box without elevated hardware privileges.** If you just run the binary normally, it will launch, but the hotkey and clicking engine will be completely unresponsive. You have two options to run it:

### Option 1: The Quick Way (Run via Sudo)
The fastest way to get up and running is to execute the binary with root privileges from your terminal:
```bash
sudo ./CachyAutoClicker