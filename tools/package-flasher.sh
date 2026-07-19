#!/bin/sh
# Package the Easel Weasel flasher with a firmware image into a
# double-click-to-flash zip for one hardware variant.
#
#   tools/package-flasher.sh <VARIANT> <hexfile> <out.zip>
#   VARIANT: V2 | REV1
#
# Run from the repo root. Uses `zip` (preserves the executable bits the
# launchers need — the #1 way these tools break is a repackaging that
# strips them).
set -e
VARIANT="$1"; HEX="$2"; OUT="$3"
[ -n "$VARIANT" ] && [ -f "$HEX" ] && [ -n "$OUT" ] || {
  echo "usage: $0 <V2|REV1> <hexfile> <out.zip>" >&2; exit 2; }

STAGEDIR="$(mktemp -d)"
PKG="$STAGEDIR/MARF-Flasher-$VARIANT"
mkdir -p "$PKG"
cp -R flasher/. "$PKG/"
rm -rf "$PKG/__pycache__" "$PKG/.gitignore" "$PKG/firmware/.gitkeep"
cp "$HEX" "$PKG/firmware/"

# Variant-specific identity + warning in the config.
python3 - "$PKG/flash.config.json" "$VARIANT" <<'PYEOF'
import json, sys
path, variant = sys.argv[1], sys.argv[2]
cfg = json.load(open(path))
if variant == "REV1":
    cfg["product_name"] = "MARF (REV1 / original v1 board)"
else:
    cfg["product_name"] = "MARF (v2 board)"
cfg["enter_bootloader_hint"] = (
    ("This zip is for the ORIGINAL v1 board ONLY - flashing it on a v2 board will misbehave. "
     if variant == "REV1" else
     "This zip is for the v2 board ONLY - flashing it on an original v1 board will misbehave. ")
    + cfg["enter_bootloader_hint"])
json.dump(cfg, open(path, "w"), indent=2)
PYEOF

chmod +x "$PKG/flash.py" "$PKG/Flash-on-Mac.command" "$PKG/Flash-on-Linux.sh"
OUTABS="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
rm -f "$OUTABS"
(cd "$STAGEDIR" && zip -rq "$OUTABS" "MARF-Flasher-$VARIANT")
rm -rf "$STAGEDIR"
echo "packaged $OUTABS"
