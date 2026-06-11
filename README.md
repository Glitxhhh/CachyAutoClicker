# CachyOS / Wayland Native Key Clicker & Holder

A native C++ Qt6 auto-clicker and key-holder utility designed specifically to bypass security restrictions on modern Wayland sessions (like CachyOS, Arch Linux, or Fedora) using the Linux kernel `uinput` architecture.

<p align="left">
  <img src="icon.png" width="120" height="120" alt="CachyAutoClicker Icon">
</p>

[![Platform](https://img.shields.io/badge/platform-Linux-orange.svg)](#)
[![Built With](https://img.shields.io/badge/built%20with-C%2B%2B%20%2F%20Qt6-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](#)
[![Releases](https://img.shields.io/badge/releases-compiled%20binary-blueviolet.svg)](#)

---

## ⚠️ CRITICAL: Hardware Permission Requirements

Because this application interacts directly with kernel-level device emulation (`/dev/uinput`) to generate native keystrokes/clicks and interfaces directly with `/dev/input/` events to track your global hotkey, **it requires elevated system hardware privileges.** If launched without these permissions, the graphical window will load normally, but the background engine and hotkey toggles will remain completely unresponsive.

---

## 🚀 Using the Pre-compiled Release Binary

### 1. Download & Prepare the Binary
1. Navigate to the **Releases** tab on the right side of this GitHub repository page.
2. Download the compiled standalone `CachyAutoClicker` binary.
3. Open a terminal where you downloaded the file and mark it as executable:
   ```bash
   chmod +x CachyAutoClicker