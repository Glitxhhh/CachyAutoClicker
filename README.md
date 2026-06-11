# Mouse Keyboard Clicker Holder v2.0

A native C++ Qt6 auto-clicker and key-holder utility designed specifically to bypass security restrictions on modern Wayland sessions (like CachyOS, Arch Linux, or Fedora) using the Linux kernel `uinput` architecture.

<p align="left">
  <img src="icon.png" width="120" height="120" alt="App Icon">
</p>

[![Platform](https://img.shields.io/badge/platform-Linux-orange.svg)](#)
[![Built With](https://img.shields.io/badge/built%20with-C%2B%2B%20%2F%20Qt6-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](#)
[![Releases](https://img.shields.io/badge/releases-compiled%20binary-blueviolet.svg)](#)

---

## ⚠️ CRITICAL: Hardware Permission Requirements

Because this application interacts directly with kernel-level device emulation (`/dev/uinput`) to generate native keystrokes/clicks and interfaces directly with `/dev/input/` events to track your global hotkey, **it requires elevated system hardware privileges.** If launched without these permissions, the graphical window will load normally, but the background engine and hotkey toggles will remain completely unresponsive.

---

## 🚀 How to Run the Pre-compiled Release Binary

### 1. Download & Prepare the Binary

1. Go to the **Releases** tab on the right side of this GitHub repository page.
2. Download the compiled standalone `CachyAutoClicker` binary.
3. Open a terminal where you downloaded the file and mark it as executable:
   ```bash
   chmod +x CachyAutoClicker
   ```

### 2. Run the Application

Because this application interfaces directly with `/dev/uinput` and `/dev/input`, it **must have permission to access input hardware**. You have two supported execution paths:

---

#### 🔹 Method A — Run with Root Privileges (Quickest)

This method guarantees immediate functionality by running the binary as root:

```bash
sudo ./CachyAutoClicker
```

- ✅ No setup required
- ✅ Works instantly
- ❌ Requires entering your password every launch
- ❌ Runs entire GUI as root (not ideal long-term)

---

#### 🔹 Method B — Persistent User-Space Access (Recommended)

This method configures your system so your **normal user can access input hardware directly**, allowing you to run the app without `sudo`.

##### Step 1 — Create uinput udev Rule

```bash
echo 'KERNEL=="uinput", GROUP="input", MODE="0660"' | sudo tee /etc/udev/rules.d/99-uinput.rules
```

##### Step 2 — Reload udev Rules

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

##### Step 3 — Add Your User to the Input Group

```bash
sudo usermod -aG input $USER
```

##### Step 4 — Apply Group Changes

You **must** log out and back in (or reboot):

```bash
reboot
```

---

##### ✅ After Setup

You can now run the application normally:

```bash
./CachyAutoClicker
```

- ✅ No sudo required
- ✅ Proper long-term solution
- ✅ Safer than running full GUI as root

---

### ⚠️ Troubleshooting

If the application launches but inputs or hotkeys do not work:

- Ensure you completed **Step 4 (reboot/log out)**
- Verify group membership:

  ```bash
  groups
  ```

  You should see `input` listed

- Confirm uinput rule exists:

  ```bash
  ls /etc/udev/rules.d/99-uinput.rules
  ```

---

### 💡 Recommendation

Use **Method A** for quick testing, but switch to **Method B** for daily use. It provides proper access control without elevating the entire application to root.
