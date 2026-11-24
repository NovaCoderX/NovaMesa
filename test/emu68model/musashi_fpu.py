# Patch Musashi's m68kfpu.c for what the m68k-linux glibc's libm emits: the
# 68881 transcendental instructions (FTAN, FETOX, FLOGN, ...).  A real 68060
# traps those to software (68060.library); here they are done with the host's
# double maths, which is all a benchmark needs.  Applied by setup.sh.
import sys
p = sys.argv[1]; s = open(p).read()
ops = {0x02: 'sinh', 0x06: 'log1p', 0x08: 'expm1', 0x09: 'tanh', 0x0a: 'atan', 0x0c: 'asin',
       0x0d: 'atanh', 0x0f: 'tan', 0x10: 'exp', 0x11: 'exp2', 0x12: 'exp10', 0x14: 'log',
       0x15: 'log10', 0x16: 'log2', 0x19: 'cosh', 0x1c: 'acos'}
cases = ''.join('\t\tcase 0x%02x: REG_FP[dst] = double_to_fx80(%s(fx80_to_double(source))); SET_CONDITION_CODES(REG_FP[dst]); break;\n' % (k, v)
                for k, v in sorted(ops.items()))
anchor = '\t\tdefault:\tfatalerror("fpgen_rm_reg: unimplemented opmode %02X at %08X\\n", opmode, REG_PC-4);'
assert anchor in s, 'anchor not found'
s = s.replace(anchor, cases + anchor)
# FINT / FINTRZ through floatx80_to_int32 lose anything outside the int32
# range (glibc's floor() and friends use them on any double); round in
# floatx80 instead.
old_fint = """			sint32 temp;
			temp = floatx80_to_int32(source);
			REG_FP[dst] = int32_to_floatx80(temp);"""
new_fint = """			REG_FP[dst] = floatx80_round_to_int(source);"""
assert old_fint in s; s = s.replace(old_fint, new_fint)
old_fintrz = """			sint32 temp;
			temp = floatx80_to_int32_round_to_zero(source);
			REG_FP[dst] = int32_to_floatx80(temp);"""
new_fintrz = """			int8 saved = float_rounding_mode;
			float_rounding_mode = float_round_to_zero;
			REG_FP[dst] = floatx80_round_to_int(source);
			float_rounding_mode = saved;"""
assert old_fintrz in s; s = s.replace(old_fintrz, new_fintrz)
# FSCALE (0x26) and FGETMAN (0x1f)
more = """\t\tcase 0x26: REG_FP[dst] = double_to_fx80(ldexp(fx80_to_double(REG_FP[dst]), (int)fx80_to_double(source))); SET_CONDITION_CODES(REG_FP[dst]); break;
\t\tcase 0x1f: { int e; double m = frexp(fx80_to_double(source), &e); REG_FP[dst] = double_to_fx80(m * 2.0); SET_CONDITION_CODES(REG_FP[dst]); break; }
"""
s = s.replace(anchor, more + anchor)
# The 68040 single/double forms are decoded by stripping 0x40/0x44, which
# turns FSSQRT (0x41) and FDSQRT (0x45) into 0x01, FINT.  Square root is 0x04.
old_rd = """	} else
		round = 0;
"""
new_rd = """	} else
		round = 0;
	if (round && opmode == 0x01)
		opmode = 0x04;		/* FSSQRT, FDSQRT */
"""
assert s.count(old_rd) == 1; s = s.replace(old_rd, new_rd)
if '#include <math.h>' not in s:
    s = '#define _GNU_SOURCE\n#include <math.h>\n' + s
open(p, 'w').write(s)
