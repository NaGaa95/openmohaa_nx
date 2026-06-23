#!/bin/sh
# =============================================================================
# nx_isolate_module.sh  LD  OBJCOPY  NM  <static-lib>  <out.o>  <keep-symbol>  <prefix>
#
# Turns a game/cgame static library into a single relocatable object that is
# fully symbol-isolated from the rest of the executable:
#
#   1. ld -r --whole-archive   merges every member into one .o
#   2. every externally-visible DEFINED symbol (global OR weak, including the
#      C++ vtable/typeinfo/inline COMDAT symbols) is renamed to a unique
#      per-module name via objcopy --redefine-syms, EXCEPT the module entry
#      point (GetGameAPI / GetCGameAPI).
#
# Renaming (rather than localizing) is deliberate: localizing the weak COMDAT
# vtable/typeinfo symbols turns their COMDAT groups local, and the final linker
# then dedups them against the engine's identically-named groups and discards
# sections ("defined in discarded section"). Unique names give each module its
# own COMDAT group signatures, so nothing is deduped across the engine and the
# two modules - which is exactly what the three separate .so files achieve on
# desktop (Class/Listener/Event have different layouts per module define).
#
# Undefined references (libc, libstdc++, Recast) and local symbols are left
# untouched, so they still resolve normally at the final link.
# =============================================================================
set -eu

LD="$1"; OBJCOPY="$2"; NM="$3"; LIB="$4"; OUT="$5"; KEEP="$6"; PFX="$7"

MERGED="${OUT}.merged.o"
REDEF="${OUT}.redef.txt"

"$LD" -r --whole-archive "$LIB" --no-whole-archive -o "$MERGED"

# nm -g --defined-only prints "<addr> <type> <name>" for each external defined
# symbol. Map every such name (except the kept entry point) to prefix+name.
"$NM" -g --defined-only "$MERGED" \
    | awk -v keep="$KEEP" -v pfx="$PFX" 'NF>=3 && $NF != keep { print $NF, pfx $NF }' \
    | sort -u > "$REDEF"

"$OBJCOPY" --redefine-syms="$REDEF" "$MERGED" "$OUT"

echo "isolated $(wc -l < "$REDEF") symbols into $OUT (kept $KEEP)"
