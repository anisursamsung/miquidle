# miquidle

A modern, lightweight Wayland idle management daemon for the **Miquland** desktop ecosystem, inspired by `swayidle` and `hypridle`.

## Features
- **Wayland Protocol**: Full support for standard `ext-idle-notify-v1` idle notifications and automatic compositor-level idle inhibition.
- **Sleep & Lock Management**: Integrates with `systemd-logind` via `sd-bus` to handle session locks and acquire delay inhibitors before system suspend.
- **Flexible Configuration**: Supports modular listener definitions (`timeout`, `on-timeout`, `on-resume`) and command hooks (`lock_cmd`, `unlock_cmd`, `before_sleep_cmd`, `after_sleep_cmd`).
- **CLI Compatibility**: Supports both config file parsing and inline `swayidle`-compatible command-line arguments.
- **Signal Handling**: Responds to `SIGTERM`/`SIGINT` for clean termination (executing pending resume commands), and `SIGUSR1` to immediately trigger idle timeouts.

## Installation

### Local Build
```bash
./make.sh
```

### System-wide Install
```bash
sudo ./make.sh
```

## Configuration

Default configuration locations (checked in order):
1. `-c /path/to/config.conf` (via CLI)
2. `~/.config/miquidle/miquidle.conf`
3. `/etc/xdg/miquidle/miquidle.conf`
4. `/usr/share/miquidle/miquidle.conf`

See `assets/miquidle.conf` for a documented example configuration.

## Usage

Start the daemon using the default config:
```bash
miquidle
```

Specify a custom config file:
```bash
miquidle -c ~/.config/miquidle/custom.conf
```

Or pass commands directly on the command line:
```bash
miquidle timeout 300 'miqulock' \
         timeout 600 'wlr-randr --output eDP-1 --off' resume 'wlr-randr --output eDP-1 --on' \
         before-sleep 'miqulock'
```
