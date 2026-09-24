#!/bin/sh
cd "$(dirname "$0")/.." || exit 1

PORT=${PORT:-8741}
BIN=$(find build -type f -name peek -perm -u+x -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-)
[ -x "$BIN" ] || { echo "peek binary not found — run xmake first"; exit 1; }
command -v python3 >/dev/null || { echo "python3 not found"; exit 1; }

SL=$(mktemp)
PG=$(mktemp)
GOT=$(mktemp)
sed -e "s/__PORT__/$PORT/g" tests/http.html > "$PG"

python3 tests/http_echo.py "$PORT" > "$SL" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
i=0
while [ $i -lt 50 ]; do
    grep -q ready "$SL" && break
    i=$((i + 1))
    sleep 0.1
done
if ! grep -q ready "$SL"; then
    echo "echo server did not start"
    cat "$SL"
    kill $SRV 2>/dev/null
    exit 1
fi
sleep 0.3

"$BIN" --run 6000 "$PG" </dev/null > "$GOT" 2>&1
kill $SRV 2>/dev/null
wait $SRV 2>/dev/null

ESC=$(printf '\033')
sed -i "s/${ESC}\[[0-9;]*[A-Za-z]//g" "$GOT"

if [ "$1" = "-u" ]; then
    cp "$GOT" tests/http.txt
    echo "regenerated tests/http.txt"
    rm -f "$SL" "$PG" "$GOT"
    exit 0
fi

if diff -u tests/http.txt "$GOT"; then
    echo "http: ok"
    rm -f "$SL" "$PG" "$GOT"
else
    echo "http: FAIL"
    rm -f "$SL" "$PG"
    exit 1
fi
