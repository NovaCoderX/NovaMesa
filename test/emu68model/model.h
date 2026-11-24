/* emu68model: mark what the harness counts.  Built only into the host and
 * m68k-linux benchmarks; under real Linux the call just fails with ENOSYS.
 *
 *   model_begin();                 start counting (resets the counters)
 *   model_end("name");             stop and print the report under "name"
 */
#ifndef EMU68MODEL_H
#define EMU68MODEL_H
#include <unistd.h>
#include <sys/syscall.h>
#define MODEL_MARK 0x7F00
static inline void model_begin(void) { syscall(MODEL_MARK, 1, ""); }
static inline void model_end(const char *name) { syscall(MODEL_MARK, 2, name); }
#endif
