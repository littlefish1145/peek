#!/bin/sh
cd "$(dirname "$0")/.." || exit 1

BIN=$(find build -type f -name peek -perm -u+x -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-)
[ -x "$BIN" ] || { echo "peek binary not found — run xmake first"; exit 1; }
CBIN=$(find build -type f -name peekcss -perm -u+x -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-)
[ -x "$CBIN" ] || { echo "peekcss binary not found — run xmake build peekcss"; exit 1; }

ESC=$(printf '\033')
STRIP="${ESC}\[[0-9;]*[A-Za-z]"
UPDATE=0
[ "$1" = "-u" ] && UPDATE=1

tp=0 tf=0 bp=0 bf=0 gp=0 gf=0 sp=0 sf=0 skipped=0

for f in tests/cases/*.html; do
    base=${f%.html}
    opt=""
    [ -f "$base.opts" ] && opt=$(cat "$base.opts")
    got=$(mktemp)
    "$BIN" "$f" > "$got" 2>&1
    got_t=$(mktemp)
    sed "s/${STRIP}//g" "$got" > "$got_t"
    got_g=$(mktemp)
    "$BIN" --dump "$f" > "$got_g" 2>&1
    got_s=$(mktemp)
    "$CBIN" $opt "$f" > "$got_s" 2>&1

    if [ "$UPDATE" -eq 1 ]; then
        cp "$got_t" "$base.txt"
        cp "$got" "$base.out"
        cp "$got_g" "$base.boxes"
        cp "$got_s" "$base.style"
        tp=$((tp + 1)); bp=$((bp + 1)); gp=$((gp + 1)); sp=$((sp + 1))
        rm -f "$got" "$got_t" "$got_g" "$got_s"
        continue
    fi

    if [ -f "$base.txt" ]; then
        if cmp -s "$base.txt" "$got_t"; then
            tp=$((tp + 1))
        else
            tf=$((tf + 1))
            echo "TEXT FAIL $f"
            diff "$base.txt" "$got_t" | head -12
        fi
    else
        skipped=$((skipped + 1))
    fi

    if [ -f "$base.out" ]; then
        if cmp -s "$base.out" "$got"; then
            bp=$((bp + 1))
        else
            bf=$((bf + 1))
            echo "BYTE FAIL $f"
            prev=$(mktemp)
            sed "s/${STRIP}//g" "$base.out" > "$prev"
            diff "$prev" "$got_t" | head -12
            rm -f "$prev"
        fi
    fi

    if [ -f "$base.boxes" ]; then
        if cmp -s "$base.boxes" "$got_g"; then
            gp=$((gp + 1))
        else
            gf=$((gf + 1))
            echo "GEOM FAIL $f"
            diff "$base.boxes" "$got_g" | head -14
        fi
    fi

    if [ -f "$base.style" ]; then
        if cmp -s "$base.style" "$got_s"; then
            sp=$((sp + 1))
        else
            sf=$((sf + 1))
            echo "STYLE FAIL $f"
            diff "$base.style" "$got_s" | head -14
        fi
    fi

    rm -f "$got" "$got_t" "$got_g" "$got_s"
done

if [ "$UPDATE" -eq 1 ]; then
    echo "regenerated: $tp text, $bp byte, $gp geometry, $sp style"
    exit 0
fi

echo "text: $tp passed, $tf failed"
echo "byte: $bp passed, $bf failed"
echo "geom: $gp passed, $gf failed"
echo "style: $sp passed, $sf failed"
[ "$skipped" -gt 0 ] && echo "($skipped cases have no .txt golden yet)"
[ "$tf" -eq 0 ] && [ "$bf" -eq 0 ] && [ "$gf" -eq 0 ] && [ "$sf" -eq 0 ]
