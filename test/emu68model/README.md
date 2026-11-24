# emu68model: what code costs under Emu68, without a PiStorm

Runs a statically linked m68k-linux program (NovaMesa's `bench` and `geom`,
or the whole of FitzQuake through its host harness) on the Musashi 68k
emulator, and counts what the program would cost on a PiStorm under Emu68.
Instruction counts on x86 (callgrind) cannot show this: on Emu68 the
expensive thing is often not an instruction but a branch the JIT guesses
wrong.

The model of Emu68 is V-WARS' `testing/softbench`, as AmiDuke's
`testing/enhancebench` uses it:

- 68k code is translated in units of up to 256 instructions;
- a `Bcc` (here also `FBcc`) is guessed taken if it jumps backward by less
  than 256 bytes, otherwise not taken; the way not guessed leaves the unit;
- `BRA`/`BSR` are followed inline; `JSR`/`JMP`, and an `RTS` with no `BSR`
  to return to, leave the unit;
- leaving the unit is a *dispatch*, a trip through Emu68's main loop.

softbench fitted that to timings on the owner's PiStorm 4: about 0.64 ns an
instruction and 25 ns a dispatch. So one wrong guess costs as much as about
40 instructions. Float divides and square roots are not in that fit; they
are counted separately and charged 12 and 15 ns (a guess from the
Cortex-A72's latencies, not a measurement).

**This has not been checked against a PiStorm for NovaMesa.** The model was
fitted on V-WARS' code; whether its ranking of NovaMesa variants holds on
the real machine is the next thing to find out (compare FitzQuake `timedemo`
before and after a change the model likes).

## Setup

Needs `gcc-m68k-linux-gnu` (gcc 13, glibc) and Python 3. On Ubuntu the cross
compiler conflicts with `gcc-multilib`, which FitzQuake's 32-bit host build
needs; if both are wanted, unpack `gcc-13-m68k-linux-gnu` and
`cpp-13-m68k-linux-gnu` with `dpkg -x` instead of installing them.

    ./setup.sh               # clones Musashi into work/, builds work/harness

`musashi_fpu.py` patches Musashi's FPU, which `setup.sh` applies:

- `FSSQRT`/`FDSQRT` were decoded as `FINT` (so `sqrt(0.1)` was 0);
- `FINT`/`FINTRZ` went through a 32-bit integer and lost anything larger;
- the 68881 transcendentals glibc's libm uses (`FTAN`, `FETOX`, `FLOGN`,
  `FSCALE`, ...) were missing; they use the host's double maths here.

With those, FitzQuake renders every frame byte for byte as it does under
qemu-m68k, and `bench`'s 30 configurations hash the same.

## Running

    cd ../hostbench
    CC="m68k-linux-gnu-gcc -m68060 -fno-optimize-sibling-calls" LDFLAGS=-static TAG=m060 ./build.sh
    ../emu68model/work/harness out/bench_m060 320 240 2
    ../emu68model/work/harness -f out/geom_m060 320 240 2       # -f: float columns too

FitzQuake (see its `test/hostbench/run.sh`):

    CC="m68k-linux-gnu-gcc -m68060 -fno-optimize-sibling-calls" LDFLAGS=-static TAG=m060 ./build.sh
    MODEL=1 BIN=out/fq_m060 MODELFROM=2 ./run.sh "timedemo demo1"           # about 15 minutes
    MODEL=1 BIN=out/fq_m060 MODELFROM=100 QUITFRAMES=200 ./run.sh "timedemo demo1"   # one minute

What is counted is chosen by the program with `model_begin()` and
`model_end(name)` from `model.h` (a pseudo system call; under real Linux it
just fails). `bench` and `geom` count each configuration's timed frames;
FitzQuake counts from swap `MODELFROM` to `MODELTO` or to the end. The
program's own clock (`clock_gettime`) runs on modelled time, so the
ms/frame `bench` and `geom` print are modelled PiStorm times.

Each report gives the modelled time, instructions, dispatches (and how many
were wrong guesses, how many calls), float divides, square roots,
int<->float conversions, other float instructions, float branches, 32-bit
multiplies and divides, and 32-bit reads and writes; then the functions
costing most.

To find *which* branch dispatches:

    PCHIST=/tmp/h.txt ../emu68model/work/harness ...      # per-address counts
    ../emu68model/pchist.py out/fq_m060 /tmp/h.txt 30      # addresses dispatching most, with source line
    ../emu68model/pclines.py out/fq_m060 /tmp/h.txt 40     # cost summed by source line

The build must have `-g` (FitzQuake's has; add `CFLAGS=-g` for `bench`).

## What it is not

- Caches and memory are not modelled, nor is the cost of a JIT translation
  (code that runs once looks cheap). Instruction counts are exact; times are
  the model's.
- System calls (files, the clock) are answered by `harness.c`; the
  Picasso96 swap is a stub in all the host builds, so the swap's real cost
  is invisible here.
- The program is m68k-linux code built by gcc 13 with `-m68060`, as the
  Amiga build is, but linked against glibc rather than libnix: `memcpy`,
  `memset` and the maths library are not the ones the Amiga uses.
