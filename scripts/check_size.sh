#!/bin/sh
# Compiles the decoder for Cortex-M and checks what it costs a firmware: the code and the stack.
# Usage: scripts/check_size.sh [output directory]
#   DMCOMP_MAX_TEXT   limit of the code in bytes (default 2048)
#   DMCOMP_MAX_STACK  limit of the stack of dmcomp_inflate in bytes (default 3072)
set -e

CC=${CC:-arm-none-eabi-gcc}
SIZE=${SIZE:-arm-none-eabi-size}
MAX_TEXT=${DMCOMP_MAX_TEXT:-2048}
MAX_STACK=${DMCOMP_MAX_STACK:-3072}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$(mktemp -d)}
mkdir -p "$OUT"
failed=0

for cpu in cortex-m0plus cortex-m4 cortex-m7; do
    obj="$OUT/dmcomp_inflate-$cpu.o"
    "$CC" -mcpu=$cpu -mthumb -Os -ffunction-sections -fdata-sections -fstack-usage -std=c99 \
        -Wall -Wextra -Wpedantic -Werror -DDMCOMP_NO_ENCODER -I"$ROOT/include" -I"$ROOT/src" \
        -c "$ROOT/src/dmcomp_inflate.c" -o "$obj"

    text=$("$SIZE" "$obj" | awk 'NR==2 { print $1 }')
    data=$("$SIZE" "$obj" | awk 'NR==2 { print $2 + $3 }')
    stack=$(awk -F'\t' '$1 ~ /dmcomp_inflate$/ { print $2 }' "$OUT/dmcomp_inflate-$cpu.su")
    huff=$(awk -F'\t' '$1 ~ /huff_build$/ { print $2 }' "$OUT/dmcomp_inflate-$cpu.su")
    stack=$((stack + huff))      # huff_build is called from dmcomp_inflate: the worst case is both

    echo "$cpu: code $text B (limit $MAX_TEXT), static data $data B, stack $stack B (limit $MAX_STACK)"
    [ "$data" -eq 0 ] || { echo "  FAIL: the decoder has static data (it has to be reentrant)"; failed=1; }
    [ "$text" -le "$MAX_TEXT" ] || { echo "  FAIL: the code is too big"; failed=1; }
    [ "$stack" -le "$MAX_STACK" ] || { echo "  FAIL: the stack is too big"; failed=1; }
done

exit $failed
