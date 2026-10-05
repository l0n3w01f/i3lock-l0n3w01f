i3lock with a clock, on top of i3lock 2.16
==========================================

This is [Lixxia's i3lock fork](https://github.com/Lixxia/i3lock) (a clock in
the unlock indicator, an always-visible indicator and configurable colours)
ported onto upstream [i3lock](https://github.com/i3/i3lock) 2.16. Lixxia's
fork was last updated in 2020 on top of i3lock 2.12 and no longer builds with
current compilers.

This is a personal fork, provided as-is. It is a screen locker, so read the
code and test it yourself before relying on it.

## Features

From Lixxia's fork:
- Display changes on key-strokes and escape/backspace.
- A 12-hour clock in the unlock indicator, redrawn every minute. `--24`
  switches to a 24-hour clock.
- The unlock indicator is always displayed, not only after the first keypress.
- Command line arguments to customize colors, each taking a color in
  hexadecimal format:
  - `-o color` Specifies verification color
  - `-w color` Specifies wrong password/backspace color
  - `-l color` Specifies default/idle color
  - The given colors are used as-is for the lines and text of their
    respective states, and lightened at 20% opacity for the circle fill.
  - Without these options the colors are green/red/black for
    verify/wrong/idle respectively.

From upstream 2.13 to 2.16, which Lixxia's fork does not have:
- Fix for a crash when the XKB configuration changes while locked.
- Only Caps Lock and Num Lock are shown as active modifiers, so the indicator
  does not leak whether Shift is held.
- `-k` / `--show-keyboard-layout`.
- `explicit_bzero()` for the password buffer and a corrected `pam_end()` path.
- The meson build system.

Changes made in this port:
- The clock text was written into a 6-byte buffer that is too small for
  "12:34 PM"; it now uses a fixed, correctly sized buffer.
- The GNU C nested functions in the drawing code are ordinary static
  functions.
- Active modifiers are shown whenever they are on, in the state color,
  following upstream.
- `pam/i3lock` is upstream's file (`auth include login`), not the fork's.
- The default background color follows upstream (`a3a3a3`).

## Example Usage
`i3lock -i ~/.i3/background.png -c '#000000' -o '#191d0f' -w '#572020' -l '#ffffff' -e`

## Screenshots

The screenshots are from Lixxia's fork. The default background is now grey
rather than white; the indicator looks the same.

#### No configuration specified
![Default](/images/defaultlock.png?raw=true "")
#### Error Color
![Error](/images/defaulterror.png?raw=true "")

### Example Configuration

#### Idle
![Idle state](/images/lockscreen.png?raw=true "")
#### Key Press
![On key press](/images/lockscreenkeypress.png?raw=true "")
#### Escape/Backspace
![On escape or backspace](/images/lockscreenesc.png?raw=true "")

Background in above screenshots can be found in images/background.jpg

## Install

### Dependencies

On Debian or Ubuntu:

```
sudo apt install meson ninja-build pkgconf libpam0g-dev libcairo2-dev \
  libev-dev libx11-dev libx11-xcb-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libxcb1-dev libxcb-util-dev libxcb-xinerama0-dev libxcb-randr0-dev \
  libxcb-image0-dev libxcb-xkb-dev libxcb-xrm-dev
```

### Build

```
meson setup build
ninja -C build
```

Test the result before installing it. This locks your screen; your normal
password unlocks it:

```
./build/i3lock -n
```

### Install alongside the distribution's i3lock

Keeping your distribution's `i3lock` package installed is the simplest way to
get a correct `/etc/pam.d/i3lock`. Install this build under a different name:

```
sudo install -m 755 build/i3lock /usr/local/bin/i3lock-clock
```

`ninja -C build install` also works, but it installs the binary as `i3lock`
and a PAM file under the prefix's `etc/pam.d`.

### Optional wrapper

Many i3 configurations start the locker as
`xss-lock --transfer-sleep-lock -- i3lock --nofork`. `contrib/i3lock-wrapper`
lets that line pick up this build and your options without editing it:

```
sudo install -m 755 contrib/i3lock-wrapper /usr/local/bin/i3lock
mkdir -p ~/.config/i3lock-clock
cp contrib/options.example ~/.config/i3lock-clock/options
```

This relies on `/usr/local/bin` being ahead of `/usr/bin` in `PATH`. The
wrapper prepends the options from `~/.config/i3lock-clock/options` and runs
`/usr/local/bin/i3lock-clock`.

---

### Original README

i3lock - improved screen locker
===============================
[i3lock](https://i3wm.org/i3lock/) is a simple screen locker like slock.
After starting it, you will see a white screen (you can configure the
color/an image). You can return to your screen by entering your password.

Many little improvements have been made to i3lock over time:

- i3lock forks, so you can combine it with an alias to suspend to RAM
  (run "i3lock && echo mem > /sys/power/state" to get a locked screen
   after waking up your computer from suspend to RAM)

- You can specify either a background color or a PNG image which will be
  displayed while your screen is locked. Note that i3lock is not an image
manipulation software. If you need to resize the image to fill the screen
or similar, use existing tooling to do this before passing it to i3lock.

- You can specify whether i3lock should bell upon a wrong password.

- i3lock uses PAM and therefore is compatible with LDAP etc.
  On OpenBSD i3lock uses the bsd_auth(3) framework.

Install
-------

See [the i3lock home page](https://i3wm.org/i3lock/).

Requirements
------------
- pkg-config
- libxcb
- libxcb-util
- libpam-dev
- libcairo-dev
- libxcb-xinerama
- libxcb-randr
- libev
- libx11-dev
- libx11-xcb-dev
- libxkbcommon >= 0.5.0
- libxkbcommon-x11 >= 0.5.0
- libxcb-image
- libxcb-xrm

Running i3lock
-------------

To test i3lock, you can directly run the `i3lock` command. To get out of it,
enter your password and press enter.

For a more permanent setup, we strongly recommend using `xss-lock` so that the
screen is locked *before* your laptop suspends:

```
xss-lock --transfer-sleep-lock -- i3lock --nofork
```

On OpenBSD the `i3lock` binary needs to be setgid `auth` to call the
authentication helpers, e.g. `/usr/libexec/auth/login_passwd`.

Building i3lock
---------------
We recommend you use the provided package from your distribution. Do not build
i3lock unless you have a reason to do so.

First install the dependencies listed in requirements section, then run these
commands (might need to be adapted to your OS):
```
rm -rf build/
mkdir -p build && cd build/

meson setup -Dprefix=/usr
ninja
```

Upstream
--------
Please submit pull requests to https://github.com/i3/i3lock
