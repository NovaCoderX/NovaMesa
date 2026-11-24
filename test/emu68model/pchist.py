#!/usr/bin/env python3
"""pchist.py <program> <pchist file> [n]: the n addresses (default 30) that
dispatch most in the model, with the instruction there and its source line,
plus the busiest instructions by count.  Needs m68k-linux-gnu-objdump and
-addr2line, and a program built with -g."""
import sys, subprocess
prog, hist = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 30
rows = []
for l in open(hist):
    a, i, d = l.split(); rows.append((int(a, 16), int(i), int(d)))
tot_i = sum(r[1] for r in rows); tot_d = sum(r[2] for r in rows)
top = sorted(rows, key=lambda r: -r[2])[:n]
addrs = ['%x' % r[0] for r in top]
src = subprocess.run(['m68k-linux-gnu-addr2line', '-f', '-i', '-C', '-e', prog] + addrs, capture_output=True, text=True).stdout.split('\n')
# addr2line -i prints a variable number of lines per address; re-run one at a time for clarity
def where(a):
    out = subprocess.run(['m68k-linux-gnu-addr2line', '-f', '-i', '-e', prog, '%x' % a], capture_output=True, text=True).stdout.strip().split('\n')
    pairs = [(out[k], out[k + 1].split('/')[-1]) for k in range(0, len(out) - 1, 2)]
    return ' <- '.join('%s %s' % (f, l) for f, l in pairs[:3])
def insn(a):
    out = subprocess.run(['m68k-linux-gnu-objdump', '-d', '--start-address=0x%x' % a, '--stop-address=0x%x' % (a + 8), prog],
                         capture_output=True, text=True).stdout.strip().split('\n')
    for l in out:
        if l.strip().startswith('%x:' % a): return ' '.join(l.split('\t')[2:]).strip()
    return '?'
print('total insn %d, dispatches %d' % (tot_i, tot_d))
print('--- addresses dispatching most (share of all dispatches; times run) ---')
for a, i, d in top:
    print('%6.2f%%  %08x  %-34s run %10d  %s' % (100.0 * d / tot_d, a, insn(a)[:34], i, where(a)))
