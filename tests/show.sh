#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
BIN=$(find build -type f -name peek -perm -u+x -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-)
[ -x "$BIN" ] || { echo "peek binary not found — run xmake first"; exit 1; }

LIST="$*"
[ -z "$LIST" ] && LIST="basic link script broken longid comment entities wrap"

for c in $LIST; do
    f="tests/cases/$c.html"
    [ -f "$f" ] || continue
    echo "================ $c ================"
    echo "--- text ---"
    timeout 5 "$BIN" "$f" | sed "s/$(printf '\033')\[[0-9;]*[A-Za-z]//g"
    echo "--- geometry ---"
    timeout 5 "$BIN" --dump "$f" 2>&1 | head -30
done
