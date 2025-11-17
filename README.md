# YAFP - Yet Another Fetch Program

A lightweight, comprehensive system information tool for Linux written in C.

## Overview

YAFP is a system information fetcher that displays detailed information about your Linux system, including hardware specs, resource usage, session details, and more. It provides color-coded output with visual progress bars for resource monitoring.

## Features

### System Information

* Operating system and distribution details
* Kernel version and architecture
* Host/device model
* System uptime and boot time
* Load averages with CPU usage estimation
* Package counts (dpkg, pacman, flatpak)
* Running processes and logged-in users

### Hardware Details

* CPU model and thread count
* CPU governor and temperature
* GPU information (vendor, device ID, driver)
* Memory and swap usage with visual bars
* Disk usage and filesystem type
* Storage device type detection (SSD/HDD/NVMe)
* Battery status and charge level
* Display resolution and type (internal/external)

### Session Information

* Current shell
* Terminal emulator detection
* Window manager and session type (X11/Wayland/TTY)
* Desktop environment
* System and terminal fonts
* Keyboard layout
* TTY path and terminal size
* Network interface and IP address
* System locale

### Visual Features

* Color-coded output with ANSI escape sequences
* Progress bars for CPU load, memory, swap, and disk usage
* Organized sections for easy reading
* Multiple output modes

## Installation

### Prerequisites

* GCC or compatible C compiler
* Standard Linux development headers
* `fc-match` (fontconfig) for font detection (optional)

### Compilation

```bash
gcc -o yafp yafp.c -Wall -Wextra -O2
```

Or with all warnings:

```bash
gcc -o yafp yafp.c -std=c11 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2
```

### Installation to System

```bash
sudo install -m 755 yafp /usr/local/bin/
```

## Usage

### Basic Usage

```bash
./yafp
```

### Command-line Options

* `-h, --help` - Display help message
* `-v, --version` - Show version information
* `--no-color` - Disable ANSI color output
* `--plain` - Plain mode (no colors, full information)
* `--minimal` - Minimal mode (only core fields: OS, kernel, uptime, memory)

### Examples

Show all system information with colors:

```bash
yafp
```

Minimal output for scripting:

```bash
yafp --minimal --no-color
```

Plain text output:

```bash
yafp --plain
```

### Environment Variables

* `NO_COLOR` - If set, disables color output automatically
* Color output is automatically disabled when output is not a TTY

## Output Sections

### System Section

* Operating system
* Host model
* Kernel version
* Architecture
* Uptime and boot time
* Load averages
* CPU load percentage with progress bar
* Package counts
* Process count
* Logged-in users
* System locale
* GCC version

### Hardware Section

* CPU model and specifications
* CPU governor
* CPU temperature
* GPU information
* Display resolution and type
* Memory usage with progress bar
* Swap usage with progress bar
* Disk usage with progress bar
* Filesystem and disk type
* Network interface status
* Battery status

### Session Section

* Shell
* Terminal emulator
* Window manager
* Desktop environment
* System font
* Terminal font
* Keyboard layout
* TTY information

## Detection Features

### Automatic Detection

**Package Managers:**

* dpkg (Debian/Ubuntu)
* pacman (Arch Linux)
* flatpak (system and user)

**Terminal Emulators:**

* konsole, kitty, alacritty, gnome-terminal
* xterm, xfce4-terminal, tilix, st, urxvt
* wezterm, foot, rio, termite, hyper
* yakuake, guake, qterminal, lxterminal
* And more

**Window Managers:**

* KWin (X11/Wayland)
* Mutter/GNOME Shell
* Sway, i3, bspwm
* Openbox, Fluxbox, xmonad, awesome
* And more

**Storage Types:**

* NVMe SSD
* SATA SSD/HDD (via rotational flag)
* eMMC/SD cards
* Virtual devices

**Fonts:**

* KDE Plasma font configuration
* Konsole terminal font profiles
* Fontconfig fallback via fc-match

## Technical Details

### System Requirements

* Linux kernel 2.6.32 or later
* `/proc` filesystem mounted
* `/sys` filesystem mounted (for hardware detection)

### Information Sources

* `/proc/cpuinfo` - CPU information
* `/proc/meminfo` - Memory statistics
* `/proc/uptime` - System uptime
* `/proc/loadavg` - Load averages
* `/proc/self/mounts` - Filesystem information
* `/sys/class/` - Hardware device information
* `/sys/devices/` - CPU frequency and governor
* `/sys/block/` - Storage device details
* `/etc/os-release` - Distribution information
* `/var/lib/dpkg/status` - Debian package count
* `/var/lib/pacman/local/` - Arch package count
* Environment variables for session information

### Standards Compliance

* POSIX.1-2008
* GNU extensions for enhanced functionality

## Limitations

* Linux-only (uses Linux-specific /proc and /sys interfaces)
* Some features require specific kernel configurations
* Font detection optimized for KDE/Konsole
* GPU detection via DRM subsystem (may not work in all cases)
* CPU temperature detection varies by hardware

## Version

Current version: 0.1.0

## License

This is licensed under BSD 3-Clause. See LICENSE for more details.

## Contributing

When contributing, please ensure:

* Code follows the existing style
* Memory allocations are properly freed
* Error handling is implemented
* Features degrade gracefully when information is unavailable

## Troubleshooting

**No color output:**

* Check if `NO_COLOR` environment variable is set
* Verify output is going to a TTY
* Use `--no-color` flag explicitly

**Missing information:**

* Ensure `/proc` and `/sys` are mounted
* Check file permissions in `/sys/class/` directories
* Some features require root access for full information

**Incorrect detection:**

* Some terminal emulators may not be recognized
* Font detection may fail on non-KDE systems
* CPU temperature sensors vary by hardware

## See Also

* neofetch
* screenfetch
* fastfetch
* pfetch
