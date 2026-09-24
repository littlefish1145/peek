#!/bin/sh
cd "$(dirname "$0")/.." || exit 1

PORT=${PORT:-8731}
PORT2=$((PORT + 1))
BIN=$(find build -type f -name peek -perm -u+x -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-)
[ -x "$BIN" ] || { echo "peek binary not found — run xmake first"; exit 1; }
command -v python3 >/dev/null || { echo "python3 not found"; exit 1; }

SL=$(mktemp)
PG=$(mktemp)
GOT=$(mktemp)
DECOY=$(mktemp -d)
sed -e "s/__PORT2__/$PORT2/g" -e "s/__PORT__/$PORT/g" tests/ws.html > "$PG"

python3 tests/ws_echo.py "$PORT" > "$SL" 2>&1 &
SRV=$!
(cd "$DECOY" && python3 -m http.server "$PORT2" --bind 127.0.0.1 > /dev/null 2>&1) &
DECPID=$!
trap 'kill $SRV $DECPID 2>/dev/null' EXIT
i=0
while [ $i -lt 50 ]; do
    grep -q ready "$SL" && break
    i=$((i + 1))
    sleep 0.1
done
if ! grep -q ready "$SL"; then
    echo "echo server did not start"
    kill $SRV 2>/dev/null
    exit 1
fi
sleep 0.3

"$BIN" --run 4000 "$PG" </dev/null > "$GOT" 2>&1
kill $SRV $DECPID 2>/dev/null
wait $SRV 2>/dev/null
rm -rf "$DECOY"

ESC=$(printf '\033')
sed -i "s/${ESC}\[[0-9;]*[A-Za-z]//g" "$GOT"

if [ "$1" = "-u" ]; then
    cp "$GOT" tests/ws.txt
    echo "regenerated tests/ws.txt"
    rm -f "$SL" "$PG" "$GOT"
    exit 0
fi

if diff -u tests/ws.txt "$GOT"; then
    echo "ws: ok"
    rm -f "$SL" "$PG" "$GOT"
else
    echo "ws: FAIL"
    rm -f "$SL" "$PG"
    exit 1
fi
