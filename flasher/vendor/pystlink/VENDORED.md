Vendored from https://github.com/pavelrevak/pystlink (MIT license, see LICENSE).
Why: pyocd refuses ST-Link V2 dongles with firmware older than V2J24 — which is
exactly what ships in 288r kits. pystlink speaks the older protocol.
Local patch: lib/stlinkusb.py uses pip's `libusb-package` bundled libusb when
available (macOS without Homebrew has no system libusb).
