#!/usr/bin/env bash
# Run me on Linux to flash your device:  ./Flash-on-Linux.sh
# (or double-click and choose "Run in Terminal" if your file manager offers it)
cd "$(dirname "$0")" || exit 1
if command -v python3 >/dev/null 2>&1; then
  exec python3 flash.py "$@"
fi
cat <<'EOF'

  I need Python 3 and can't find it. Install it with your package manager:

    Debian/Ubuntu:  sudo apt install python3
    Fedora:         sudo dnf install python3
    Arch:           sudo pacman -S python

  Then run this script again.

EOF
read -r -p "Press Enter to close."
