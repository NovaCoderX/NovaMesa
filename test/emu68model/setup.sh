#!/bin/sh
# setup.sh [workdir]: fetch Musashi (MIT licence) at a fixed commit, turn on
# its instruction hook for harness.c, and build the harness into workdir
# (./work by default).  Musashi stays out of the repo.
set -e
D=$(cd "$(dirname "$0")" && pwd)
W=${1:-$D/work}
mkdir -p "$W"; W=$(cd "$W" && pwd)
if [ ! -d "$W/musashi" ]; then
    git clone -q https://github.com/kstenerud/Musashi.git "$W/musashi"
    (cd "$W/musashi" && git checkout -q 313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd)
fi
rm -rf "$W/emu"; mkdir -p "$W/emu"
cp -r "$W/musashi/m68k_in.c" "$W/musashi/m68kcpu.c" "$W/musashi/m68kcpu.h" "$W/musashi/m68k.h" \
      "$W/musashi/m68kconf.h" "$W/musashi/m68kdasm.c" "$W/musashi/m68kfpu.c" "$W/musashi/m68kmake.c" \
      "$W/musashi/m68kmmu.h" "$W/musashi/softfloat" "$W/emu/"
sed -i 's/#define M68K_INSTRUCTION_HOOK       M68K_OPT_OFF/#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER/' "$W/emu/m68kconf.h"
sed -i 's/#define M68K_INSTRUCTION_CALLBACK(pc) your_instruction_hook_function(pc)/void my_hook(unsigned int pc);\n#define M68K_INSTRUCTION_CALLBACK(pc) my_hook(pc)/' "$W/emu/m68kconf.h"
grep -q "my_hook" "$W/emu/m68kconf.h"
# FPU fixes gcc's 68060 code needs, see musashi_fpu.py
python3 "$D/musashi_fpu.py" "$W/emu/m68kfpu.c"
gcc -O2 -o "$W/emu/m68kmake" "$W/emu/m68kmake.c"
(cd "$W/emu" && ./m68kmake "$W/emu" "$W/emu/m68k_in.c" >/dev/null)
gcc -O2 -w -I"$W/emu" -o "$W/harness" "$D/harness.c" "$W/emu/m68kcpu.c" "$W/emu/m68kops.c" \
    "$W/emu/m68kdasm.c" "$W/emu/softfloat/softfloat.c" -lm
echo "harness built: $W/harness"
