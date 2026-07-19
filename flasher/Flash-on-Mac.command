#!/bin/bash
# Double-click me on a Mac to flash your device.
cd "$(dirname "$0")" || exit 1
if command -v python3 >/dev/null 2>&1; then
  exec python3 flash.py "$@"
fi
cat <<'EOF'

  Oops — I need Python 3, and it doesn't look installed.

  The easy fix (takes 2 minutes):
    1. Go to https://www.python.org/downloads/
    2. Download and run the macOS installer.
    3. Double-click this file again.

  (On most Macs Python is already there — if this keeps happening,
   send the maintainer a screenshot and we'll sort it out.)

EOF
read -r -p "Press Enter to close this window."
