/*
 * emu68model: run a statically linked m68k-linux program under Musashi and
 * count what it would cost under Emu68 (the PiStorm's JIT).
 *
 * The cost model is the one from V-WARS' testing/softbench, as used by
 * AmiDuke's testing/enhancebench, unchanged except that FBcc is treated like
 * Bcc (softbench never met float code):
 *
 *   Emu68 translates the 68k code into units of up to 256 instructions.  A
 *   Bcc/FBcc is guessed taken if it jumps backward by less than 256 bytes and
 *   not taken otherwise; the way not guessed leaves the unit (a "dispatch":
 *   a trip back through Emu68's main loop).  BRA and BSR are followed inline,
 *   JSR/JMP leave the unit, so does an RTS with no BSR to return to.  A
 *   branch back to the start of the unit is a loop and does not leave it.
 *
 * softbench fitted that to the PiStorm 4's times: about 0.64 ns an
 * instruction and 25 ns a dispatch.  Float instructions are not in that fit;
 * they are counted separately (see the report) so they can be weighed.
 *
 * Linux system calls (trap #0) are answered by the harness, enough for a
 * static glibc program that computes and writes files.  A program marks
 * what to count with the pseudo system call MODEL_MARK (see model.h).
 *
 * clock_gettime() returns the model's own time (0.64 ns an instruction,
 * 25 ns a dispatch, and FDIV_NS / FSQRT_NS for those), so a benchmark's own
 * timings come out as modelled PiStorm times.
 *
 * Usage: harness [-q] [-f] program [args...]
 *   -q   no per-function table, totals only
 *   -f   per-function table with the float column breakdown
 * With PCHIST=file in the environment, each report also writes, for every
 * instruction address, the times it ran and the dispatches that left from
 * it (pchist.py turns that into source lines).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "m68k.h"

/* ---- costs (ns) ---- */
#define INSN_NS   0.64
#define DISP_NS   25.0
/* Not fitted: a Cortex-A72 double divide or square root is roughly 18-30
 * cycles at 1.8 GHz against about one cycle for an ordinary instruction. */
#define FDIV_NS   12.0
#define FSQRT_NS  15.0

#define MODEL_MARK 0x7F00     /* pseudo system call number, see model.h */

/* ---- memory: the whole 32-bit space, reserved, touched on demand ---- */
static uint8_t *mem;
static inline uint32_t rd32(uint32_t a) { return (mem[a]<<24)|(mem[a+1]<<16)|(mem[a+2]<<8)|mem[a+3]; }
static inline uint16_t rd16(uint32_t a) { return (mem[a]<<8)|mem[a+1]; }
static inline void wr32(uint32_t a, uint32_t v) { mem[a]=v>>24; mem[a+1]=v>>16; mem[a+2]=v>>8; mem[a+3]=v; }
static inline void wr16(uint32_t a, uint32_t v) { mem[a]=v>>8; mem[a+1]=v; }

#define TRAPSTUB  0x00001000u   /* trap #0 vector points here: an RTE */
#define STACKTOP  0xBFFF0000u
#define MMAPBASE  0x40000000u
static uint32_t brkBase, brkCur, mmapCur = MMAPBASE, tp;

/* ---- functions, from the ELF symbol table ---- */
#define MAXFN 8192
typedef struct { uint32_t a, e; const char *name;
                 uint64_t insn, disp, mispred, fdiv, fsqrt, fconv, fother, fbcc, mul, div, rd, wr; } Fn;
static Fn fn[MAXFN]; static int nfn;
static uint32_t textLo, textHi; static uint16_t *fnOf;   /* per halfword of text: index + 1 */
static int curFn = -1;
static uint32_t *pcIns, *pcDisp;   /* PCHIST: per halfword of text */

/* ---- the model ---- */
static int measuring;
static uint32_t prevPC = 0xFFFFFFFF;
static uint32_t unitStart, unitCount, lastRev = 0xFFFFFFFF; static int maxRev, retDepth;
typedef struct { uint64_t insn, disp, mispred, fdiv, fsqrt, fconv, fother, fbcc, mul, div, rd, wr, jsr, bt, bn; } Tot;
static Tot tot, all;   /* tot: the marked region; all: since start, for the clock */
static uint64_t allInsnAny, allDispAny;

static inline Fn *fn_at(uint32_t pc)
{
    if (pc < textLo || pc >= textHi) return NULL;
    uint16_t k = fnOf[(pc - textLo) >> 1];
    return k ? &fn[k - 1] : NULL;
}

static void dispatch(uint32_t pc, int why, uint32_t from)
{
    allDispAny++;
    if (measuring) { tot.disp++; if (why) tot.mispred++;
        Fn *f = fn_at(from); if (f) { f->disp++; if (why) f->mispred++; }
        if (pcDisp && from >= textLo && from < textHi) pcDisp[(from - textLo) >> 1]++; }
    unitStart = pc; unitCount = 0; lastRev = 0xFFFFFFFF; retDepth = 0;
}

static void backward(uint32_t target, uint32_t pc)
{
    if (target == unitStart) { unitCount = 0; return; }        /* inner loop */
    if (lastRev == target) { if (--maxRev == 0) dispatch(pc, 0, prevPC); }
    else { lastRev = target; maxRev = 7; }
}

static void cond_branch(uint32_t pc, uint32_t p, uint32_t target, uint32_t next)
{
    int taken = (pc == target) && target != next;
    if (measuring) { if (taken) tot.bt++; else tot.bn++; }
    int guess = target < next && (next - target) < 256;
    if (taken != guess) { dispatch(pc, 1, p); return; }
    if (taken) backward(target, pc);
}

static void transition(uint32_t pc)
{
    uint32_t p = prevPC;
    uint16_t op = rd16(p);
    if ((op & 0xF000) == 0x6000) {
        int cond = (op >> 8) & 15; int32_t d = (int8_t)(op & 0xFF); uint32_t len = 2;
        if ((op & 0xFF) == 0) { d = (int16_t)rd16(p+2); len = 4; }
        else if ((op & 0xFF) == 0xFF) { d = (int32_t)rd32(p+2); len = 6; }
        uint32_t target = p + 2 + d, next = p + len;
        if (cond == 0) { if (target < next) backward(target, pc); return; }  /* BRA inlined */
        if (cond == 1) { retDepth++; return; }                                /* BSR inlined */
        cond_branch(pc, p, target, next);
        return;
    }
    if ((op & 0xFF80) == 0xF280) {                     /* FBcc.w / FBcc.l */
        uint32_t target, next;
        if (op & 0x40) { target = p + 2 + (int32_t)rd32(p+2); next = p + 6; }
        else { target = p + 2 + (int16_t)rd16(p+2); next = p + 4; }
        if (measuring) { tot.fbcc++; Fn *f = fn_at(p); if (f) f->fbcc++; }
        cond_branch(pc, p, target, next);
        return;
    }
    if ((op & 0xF0F8) == 0x50C8) {                     /* DBcc */
        uint32_t target = p + 2 + (int16_t)rd16(p+2);
        if (pc != target) { dispatch(pc, 1, p); return; }
        backward(target, pc); return;
    }
    if ((op & 0xFFC0) == 0x4E80 || (op & 0xFFC0) == 0x4EC0) {    /* JSR, JMP */
        if (measuring) tot.jsr++;
        dispatch(pc, 0, p); return;
    }
    if (op == 0x4E75) {                                /* RTS */
        if (retDepth > 0) { retDepth--; return; }
        dispatch(pc, 0, p); return;
    }
}

/* classify the instruction at pc (68k opcode, and the FPU command word) */
static void classify(uint32_t pc, Fn *f)
{
    uint16_t o = rd16(pc);
    if ((o & 0xFFC0) == 0x4C00 || (o & 0xF0C0) == 0xC0C0) { tot.mul++; if (f) f->mul++; return; }
    if ((o & 0xFFC0) == 0x4C40 || (o & 0xF0C0) == 0x80C0) { tot.div++; if (f) f->div++; return; }
    if ((o & 0xFFC0) == 0xF200) {                      /* FPU general */
        uint16_t w = rd16(pc + 2);
        int cls = w >> 13, opm = w & 0x7F;
        if (cls == 0 || cls == 2) {
            int src = (w >> 10) & 7;
            int base = opm & 0x3F;
            if (opm >= 0x40) base = opm & ~0x44 & 0x7F;
            if (base == 0x20 || base == 0x24 || (opm & 0x7B) == 0x60) { tot.fdiv++; if (f) f->fdiv++; return; }
            if (base == 0x04 || (opm & 0x7B) == 0x41) { tot.fsqrt++; if (f) f->fsqrt++; return; }
            /* an integer source converted to float */
            if (cls == 2 && base == 0 && (src == 0 || src == 4 || src == 6)) { tot.fconv++; if (f) f->fconv++; return; }
        }
        if (cls == 3) {                                /* FMOVE FPn,<ea> */
            int dfmt = (w >> 10) & 7;
            if (dfmt == 0 || dfmt == 4 || dfmt == 6) { tot.fconv++; if (f) f->fconv++; return; }
        }
        tot.fother++; if (f) f->fother++;
    }
}

/* ---- Linux system calls ---- */
static int fds[64];
static uint32_t cstr(uint32_t a, char *buf, int n) { int i; for (i = 0; i < n - 1 && mem[a+i]; i++) buf[i] = mem[a+i]; buf[i] = 0; return i; }

static double model_ns(void)
{
    return all.insn * INSN_NS + allDispAny * DISP_NS + all.fdiv * FDIV_NS + all.fsqrt * FSQRT_NS;
}

static void report(const char *name);
static int quiet, fpcols;

static void put_stat64(uint32_t a, struct stat *st)
{
    /* struct stat64 for m68k (asm/stat.h): 96 bytes */
    memset(mem + a, 0, 96);
    wr32(a + 0, (uint32_t)st->st_dev >> 0); /* st_dev is 64-bit + pad; low half is fine */
    wr32(a + 12, (uint32_t)st->st_ino);
    wr32(a + 16, st->st_mode); wr32(a + 20, st->st_nlink);
    wr32(a + 24, st->st_uid); wr32(a + 28, st->st_gid);
    wr32(a + 44, (uint32_t)(st->st_size >> 32)); wr32(a + 48, (uint32_t)st->st_size);
    wr32(a + 52, 4096);
    wr32(a + 60, (uint32_t)st->st_blocks);
    wr32(a + 88, 0); wr32(a + 92, (uint32_t)st->st_ino);
}

static uint32_t do_syscall(void)
{
    uint32_t nr = m68k_get_reg(NULL, M68K_REG_D0);
    uint32_t a1 = m68k_get_reg(NULL, M68K_REG_D1), a2 = m68k_get_reg(NULL, M68K_REG_D2);
    uint32_t a3 = m68k_get_reg(NULL, M68K_REG_D3), a4 = m68k_get_reg(NULL, M68K_REG_D4);
    char path[1024];
    switch (nr) {
    case 1: case 247:                                  /* exit, exit_group */
        if (measuring) report("(exit)");
        fflush(stdout); exit(a1);
    case 3: { if (a1 >= 64 || !fds[a1]) return -EBADF;  /* read */
        ssize_t r = read(fds[a1] - 1, mem + a2, a3); return r < 0 ? -errno : r; }
    case 4: { if (a1 >= 64 || !fds[a1]) return -EBADF;  /* write */
        ssize_t r = write(fds[a1] - 1, mem + a2, a3); return r < 0 ? -errno : r; }
    case 146: { uint32_t n = 0; if (a1 >= 64 || !fds[a1]) return -EBADF;   /* writev */
        for (uint32_t i = 0; i < a3; i++) { uint32_t b = rd32(a2 + i*8), l = rd32(a2 + i*8 + 4);
            ssize_t r = write(fds[a1] - 1, mem + b, l); if (r < 0) return -errno; n += r; }
        return n; }
    case 5: case 288: {                                /* open, openat */
        uint32_t pa = nr == 5 ? a1 : a2, fl = nr == 5 ? a2 : a3, md = nr == 5 ? a3 : a4;
        cstr(pa, path, sizeof path);
        int hf = 0; if (fl & 03) hf |= (fl & 03) == 1 ? O_WRONLY : O_RDWR;
        if (fl & 0100) hf |= O_CREAT; if (fl & 01000) hf |= O_TRUNC; if (fl & 02000) hf |= O_APPEND;
        int h = open(path, hf, md & 0777); if (h < 0) return -errno;
        for (int i = 3; i < 64; i++) if (!fds[i]) { fds[i] = h + 1; return i; }
        close(h); return -EMFILE; }
    case 6: if (a1 < 64 && fds[a1]) { if (a1 > 2) close(fds[a1] - 1); fds[a1] = 0; return 0; } return -EBADF;
    case 19: { if (a1 >= 64 || !fds[a1]) return -EBADF; off_t r = lseek(fds[a1] - 1, (int32_t)a2, a3); return r < 0 ? -errno : r; }
    case 140: { if (a1 >= 64 || !fds[a1]) return -EBADF;           /* _llseek */
        off_t r = lseek(fds[a1] - 1, ((off_t)a2 << 32) | a3, m68k_get_reg(NULL, M68K_REG_D5));
        if (r < 0) return -errno; wr32(a4, (uint32_t)(r >> 32)); wr32(a4 + 4, (uint32_t)r); return 0; }
    case 197: { struct stat st; if (a1 >= 64 || !fds[a1]) return -EBADF;    /* fstat64 */
        if (fstat(fds[a1] - 1, &st)) return -errno; put_stat64(a2, &st); return 0; }
    case 195: { struct stat st; cstr(a1, path, sizeof path); if (stat(path, &st)) return -errno; put_stat64(a2, &st); return 0; }
    case 293: { struct stat st; cstr(a2, path, sizeof path);       /* fstatat64 */
        if (!path[0] && a1 < 64 && fds[a1]) { if (fstat(fds[a1] - 1, &st)) return -errno; }
        else if (stat(path, &st)) return -errno;
        put_stat64(a3, &st); return 0; }
    case 379: return -ENOSYS;                           /* statx: glibc falls back */
    case 33: case 300: case 439: { cstr(nr == 33 ? a1 : a2, path, sizeof path); return access(path, nr == 33 ? a2 : a3) ? -errno : 0; }
    case 39: { cstr(a1, path, sizeof path); return mkdir(path, a2) ? -errno : 0; }
    case 54: return -ENOTTY;                            /* ioctl */
    case 45:                                            /* brk */
        if (a1 >= brkBase && a1 < MMAPBASE) brkCur = a1;
        return brkCur;
    case 192: {                                         /* mmap2 */
        uint32_t len = (a2 + 0xFFF) & ~0xFFFu;
        uint32_t fl = a4, fd = m68k_get_reg(NULL, M68K_REG_D5);
        if (!(fl & 0x20)) return -ENODEV;               /* only anonymous */
        (void)fd;
        uint32_t r = mmapCur; mmapCur += len;
        if (mmapCur >= 0xB0000000u) return -ENOMEM;
        memset(mem + r, 0, len);
        return r; }
    case 91: case 125: case 238: return 0;              /* munmap, mprotect, madvise */
    case 163: return -ENOMEM;                           /* mremap: glibc copies instead */
    case 333: return tp;                                /* get_thread_area */
    case 334: tp = a1; return 0;                        /* set_thread_area */
    case 253: return 1;                                 /* set_tid_address */
    case 304: case 384: return -ENOSYS;                 /* set_robust_list, rseq */
    case 20: case 221: return 1;                        /* getpid, gettid */
    case 64: return 0;
    case 199: case 200: case 201: case 202: return 0;
    case 174: case 175: return 0;                       /* signals: ignored */
    case 339: case 191: return -ENOSYS;                 /* prlimit64, ugetrlimit */
    case 352: { for (uint32_t i = 0; i < a2; i++) mem[a1 + i] = (uint8_t)(i * 37 + 11); return a2; }   /* getrandom */
    case 122: { memset(mem + a1, 0, 390); strcpy((char *)mem + a1, "Linux"); strcpy((char *)mem + a1 + 65*2, "5.0.0");
                strcpy((char *)mem + a1 + 65*4, "m68k"); return 0; }   /* uname */
    case 85: case 298: return -ENOENT;                  /* readlink */
    case 183: { if (!getcwd(path, sizeof path)) return -errno; uint32_t l = strlen(path) + 1; if (l > a2) return -ERANGE;
                memcpy(mem + a1, path, l); return l; }
    case 260: case 403: {                               /* clock_gettime: model time */
        double ns = model_ns(); uint64_t s = (uint64_t)(ns / 1e9), n = (uint64_t)(ns - s * 1e9);
        if (nr == 260) { wr32(a2, (uint32_t)s); wr32(a2 + 4, (uint32_t)n); }
        else { wr32(a2, 0); wr32(a2 + 4, (uint32_t)s); wr32(a2 + 8, 0); wr32(a2 + 12, (uint32_t)n); }
        return 0; }
    case 78: { double ns = model_ns(); uint64_t s = (uint64_t)(ns / 1e9);
        if (a1) { wr32(a1, (uint32_t)s); wr32(a1 + 4, (uint32_t)((ns - s * 1e9) / 1000)); } return 0; }
    case 13: { uint32_t s = (uint32_t)(model_ns() / 1e9); if (a1) wr32(a1, s); return s; }
    case 43: { uint32_t t = (uint32_t)(model_ns() / 1e7); if (a1) { wr32(a1, t); wr32(a1+4, 0); wr32(a1+8, 0); wr32(a1+12, 0); } return t; }
    case 162: case 407: case 262: case 158: return 0;   /* sleeps */
    case 235: case 422: return 0;                       /* futex: single thread */
    case 239: return 0;                                 /* fcntl64 */
    case 116: return -ENOSYS;
    case MODEL_MARK: {                                  /* a1: 1 start, 2 stop and report; a2: name */
        if (a1 == 1) {
            memset(&tot, 0, sizeof tot);
            for (int i = 0; i < nfn; i++) { Fn *f = &fn[i]; const char *n = f->name; uint32_t a = f->a, e = f->e;
                memset(f, 0, sizeof *f); f->name = n; f->a = a; f->e = e; }
            if (pcIns) { memset(pcIns, 0, (textHi - textLo) / 2 * 4); memset(pcDisp, 0, (textHi - textLo) / 2 * 4); }
            measuring = 1;
        } else if (a1 == 2) {
            measuring = 0;
            cstr(a2, path, sizeof path);
            report(path);
        }
        return 0; }
    default:
        fprintf(stderr, "emu68model: unhandled system call %u\n", nr);
        return -ENOSYS;
    }
}

/* ---- the instruction hook ---- */
void my_hook(unsigned int pc)
{
    if (pc == TRAPSTUB) {                               /* trap #0 landed: answer it, the RTE returns */
        m68k_set_reg(M68K_REG_D0, do_syscall());
        prevPC = 0xFFFFFFFF;
        return;
    }
    if (prevPC != 0xFFFFFFFF) transition(pc);
    if (unitCount >= 256) dispatch(pc, 0, prevPC);
    unitCount++;
    all.insn++;
    if (measuring) {
        Fn *f = fn_at(pc);
        tot.insn++; if (f) f->insn++;
        if (pcIns && pc >= textLo && pc < textHi) pcIns[(pc - textLo) >> 1]++;
        classify(pc, f);
        curFn = f ? (int)(f - fn) : -1;
    }
    prevPC = pc;
    /* the trap itself: the next instruction is the stub, which is not the program's */
    if (rd16(pc) == 0x4E40) prevPC = 0xFFFFFFFF;
}

/* ---- memory callbacks ---- */
unsigned int m68k_read_memory_8(unsigned int a) { return mem[a]; }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a)
{
    if (measuring) { tot.rd++; if (curFn >= 0) fn[curFn].rd++; }
    return rd32(a);
}
void m68k_write_memory_8(unsigned int a, unsigned int v) { mem[a] = v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    if (measuring) { tot.wr++; if (curFn >= 0) fn[curFn].wr++; }
    wr32(a, v);
}
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd32(a); }

/* ---- report ---- */
static double fn_ns(const Fn *f) { return f->insn * INSN_NS + f->disp * DISP_NS + f->fdiv * FDIV_NS + f->fsqrt * FSQRT_NS; }
static int cmp_fn(const void *x, const void *y)
{
    double a = fn_ns(*(Fn * const *)x), b = fn_ns(*(Fn * const *)y);
    return a < b ? 1 : a > b ? -1 : 0;
}

static void report(const char *name)
{
    double ns = tot.insn * INSN_NS + tot.disp * DISP_NS + tot.fdiv * FDIV_NS + tot.fsqrt * FSQRT_NS;
    printf("MODEL %-28s %.3f ms | insn %llu disp %llu (mispred %llu, jsr %llu) | fdiv %llu fsqrt %llu fconv %llu fother %llu fbcc %llu | muls %llu divs %llu | rd32 %llu wr32 %llu\n",
           name, ns / 1e6, (unsigned long long)tot.insn, (unsigned long long)tot.disp, (unsigned long long)tot.mispred,
           (unsigned long long)tot.jsr, (unsigned long long)tot.fdiv, (unsigned long long)tot.fsqrt,
           (unsigned long long)tot.fconv, (unsigned long long)tot.fother, (unsigned long long)tot.fbcc,
           (unsigned long long)tot.mul, (unsigned long long)tot.div,
           (unsigned long long)tot.rd, (unsigned long long)tot.wr);
    if (pcIns && getenv("PCHIST")) {
        FILE *o = fopen(getenv("PCHIST"), "w");
        if (o) { for (uint32_t k = 0; k < (textHi - textLo) / 2; k++) if (pcIns[k] || pcDisp[k])
                     fprintf(o, "%08x %u %u\n", textLo + k * 2, pcIns[k], pcDisp[k]);
                 fclose(o); }
    }
    if (quiet || ns <= 0) { fflush(stdout); return; }
    static Fn *order[MAXFN];
    int n = 0;
    for (int i = 0; i < nfn; i++) if (fn[i].insn) order[n++] = &fn[i];
    qsort(order, n, sizeof order[0], cmp_fn);
    for (int i = 0; i < n && i < 25; i++) {
        Fn *f = order[i]; double fns = fn_ns(f);
        if (fns < ns * 0.002) break;
        printf("  %5.1f%%  %-34.34s insn %11llu disp %9llu (mis %8llu)", 100.0 * fns / ns, f->name,
               (unsigned long long)f->insn, (unsigned long long)f->disp, (unsigned long long)f->mispred);
        if (fpcols) printf(" fdiv %7llu fsqrt %6llu fconv %8llu fother %9llu fbcc %8llu",
               (unsigned long long)f->fdiv, (unsigned long long)f->fsqrt, (unsigned long long)f->fconv,
               (unsigned long long)f->fother, (unsigned long long)f->fbcc);
        printf("\n");
    }
    fflush(stdout);
}

/* ---- ELF loader ---- */
static uint32_t be32(const uint8_t *p) { return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }
static uint16_t be16(const uint8_t *p) { return (p[0]<<8)|p[1]; }
static int cmp_sym(const void *x, const void *y) { const Fn *a = x, *b = y; return a->a < b->a ? -1 : a->a > b->a; }

static uint32_t phdrAddr, phnum, entry;
static void load_elf(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) { perror("read"); exit(1); } fclose(f);
    if (memcmp(b, "\177ELF", 4) || b[4] != 1 || b[5] != 2 || be16(b + 18) != 4) { fprintf(stderr, "not an m68k ELF32 file\n"); exit(1); }
    entry = be32(b + 24);
    uint32_t phoff = be32(b + 28), shoff = be32(b + 32);
    uint16_t phent = be16(b + 42), shent = be16(b + 46), shnum = be16(b + 48);
    phnum = be16(b + 44);
    uint32_t end = 0;
    for (int i = 0; i < (int)phnum; i++) {
        const uint8_t *p = b + phoff + i * phent;
        uint32_t type = be32(p), off = be32(p + 4), va = be32(p + 8), fsz = be32(p + 16), msz = be32(p + 20);
        if (type == 1) {
            memcpy(mem + va, b + off, fsz); memset(mem + va + fsz, 0, msz - fsz);
            if (va + msz > end) end = va + msz;
            if (off == 0) phdrAddr = va + phoff;
            if (be32(p + 24) & 1) { if (!textLo || va < textLo) textLo = va; if (va + msz > textHi) textHi = va + msz; }
        }
    }
    brkBase = brkCur = (end + 0xFFF) & ~0xFFFu;
    /* function symbols */
    for (int i = 0; i < shnum; i++) {
        const uint8_t *s = b + shoff + i * shent;
        if (be32(s + 4) != 2) continue;                 /* SHT_SYMTAB */
        uint32_t off = be32(s + 16), sz = be32(s + 20), link = be32(s + 24), es = be32(s + 36);
        const uint8_t *strs = b + shoff + link * shent;
        const char *str = (const char *)b + be32(strs + 16);
        for (uint32_t k = 0; k < sz / es && nfn < MAXFN; k++) {
            const uint8_t *y = b + off + k * es;
            uint32_t nm = be32(y), va = be32(y + 4), size = be32(y + 8); uint8_t info = y[12];
            if ((info & 15) != 2 || !va) continue;      /* STT_FUNC */
            fn[nfn].a = va; fn[nfn].e = va + (size ? size : 2); fn[nfn].name = strdup(str + nm); nfn++;
        }
    }
    qsort(fn, nfn, sizeof fn[0], cmp_sym);
    fnOf = calloc((textHi - textLo) / 2 + 1, 2);
    for (int i = 0; i < nfn; i++)
        for (uint32_t a = fn[i].a; a < fn[i].e && a < textHi; a += 2)
            if (a >= textLo) fnOf[(a - textLo) >> 1] = i + 1;
    free(b);
}

int main(int argc, char **argv)
{
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        if (!strcmp(argv[ai], "-q")) quiet = 1; else if (!strcmp(argv[ai], "-f")) fpcols = 1;
        ai++;
    }
    if (ai >= argc) { fprintf(stderr, "usage: harness [-q] [-f] program [args...]\n"); return 1; }
    mem = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }
    load_elf(argv[ai]);
    if (getenv("PCHIST")) { pcIns = calloc((textHi - textLo) / 2 + 1, 4); pcDisp = calloc((textHi - textLo) / 2 + 1, 4); }
    fds[0] = 1; fds[1] = 2; fds[2] = 3;
    /* trap #0 -> the stub */
    wr32(32 * 4, TRAPSTUB); wr16(TRAPSTUB, 0x4E73);
    /* the initial stack: argc, argv, envp, auxv, then the strings */
    uint32_t sp = STACKTOP - 0x20000, strp = STACKTOP - 0x10000, rnd = STACKTOP - 0x10;
    int pargc = argc - ai;
    uint32_t argvp[64];
    for (int i = 0; i < pargc && i < 63; i++) { size_t l = strlen(argv[ai + i]) + 1; memcpy(mem + strp, argv[ai + i], l); argvp[i] = strp; strp += l; }
    for (int i = 0; i < 16; i++) mem[rnd + i] = i * 13 + 7;
    uint32_t q = sp;
    wr32(q, pargc); q += 4;
    for (int i = 0; i < pargc; i++) { wr32(q, argvp[i]); q += 4; }
    wr32(q, 0); q += 4;
    /* the environment, passed through */
    extern char **environ;
    for (char **e = environ; *e; e++) { size_t l = strlen(*e) + 1;
        if (strp + l > STACKTOP - 0x20) break;
        memcpy(mem + strp, *e, l); wr32(q, strp); q += 4; strp += l; }
    wr32(q, 0); q += 4;
    uint32_t aux[][2] = { {3, phdrAddr}, {4, 32}, {5, phnum}, {6, 4096}, {9, entry}, {11, 0}, {12, 0}, {13, 0}, {14, 0},
                          {16, 0}, {17, 100}, {23, 0}, {25, rnd}, {0, 0} };
    for (int i = 0; aux[i][0] || i == 0 || aux[i-1][0]; i++) { wr32(q, aux[i][0]); wr32(q + 4, aux[i][1]); q += 8; if (!aux[i][0]) break; }

    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68040); m68k_pulse_reset();
    m68k_set_reg(M68K_REG_VBR, 0);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, entry);
    unitStart = entry;
    for (;;) m68k_execute(1000000);
}
