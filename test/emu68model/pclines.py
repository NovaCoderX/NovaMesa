#!/usr/bin/env python3
"""pclines.py <program> <pchist file> [n]: dispatches and instructions summed
by source line (innermost inlined location), busiest n lines by modelled
cost (0.64 ns an instruction, 25 ns a dispatch)."""
import sys, subprocess, collections
prog, hist = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
rows = [tuple(int(x, 16) if k == 0 else int(x) for k, x in enumerate(l.split())) for l in open(hist)]
out = subprocess.run(['m68k-linux-gnu-addr2line', '-e', prog] + ['%x' % r[0] for r in rows],
                     capture_output=True, text=True).stdout.split('\n')
agg = collections.defaultdict(lambda: [0, 0])
for r, loc in zip(rows, out):
    loc = loc.split('/')[-1].split(' (')[0]
    agg[loc][0] += r[1]; agg[loc][1] += r[2]
ti = sum(v[0] for v in agg.values()); td = sum(v[1] for v in agg.values())
tot = ti * 0.64 + td * 25
print('total: insn %d disp %d, %.1f ms' % (ti, td, tot / 1e6))
for loc, (i, d) in sorted(agg.items(), key=lambda kv: -(kv[1][0] * 0.64 + kv[1][1] * 25))[:n]:
    print('%5.1f%%  %-32s insn %11d  disp %9d' % (100 * (i * 0.64 + d * 25) / tot, loc, i, d))
