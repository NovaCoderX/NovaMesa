# NovaMesa — notes for Claude

Software OpenGL for Amiga 68k RTG, based on Mesa 4.1 with a thin Picasso96
driver. Practical target is PiStorm 4 (Emu68), which runs on one Pi core, so
per-pixel cost in the software rasteriser is what limits frame rate.

The owner cross-compiles with gcc 13 from Eclipse CDT (there is no Makefile in
the repo) using:

    -O2 -m68060 -noixemul -fno-strict-aliasing -fno-optimize-sibling-calls
    -fmessage-length=0 -trigraphs -Wno-trigraphs

## Ground rules

- **Performance work must be general.** Fast paths are chosen from OpenGL state
  only, never from knowledge of a particular application. Anything a fast path
  does not fully handle must fall through to the original Mesa code.
- **Prefer fixed point in inner loops.** The 68060 FPU is slow relative to its
  integer unit and float<->int conversions are expensive. Float work per
  triangle or per 16-pixel run is fine; float work per pixel is not.
- **C first, assembler only on evidence.** On Emu68 the 68k code is translated
  to ARM, and the 68k FPU has no SIMD, so hand-written 68k assembly rarely
  beats gcc -O2. Only consider it for one specific loop that a profile shows is
  hot *and* whose gcc output is clearly poor.
- Changes are marked with a `NOVA:` comment explaining why, matching the
  existing convention.
- Line endings are mixed. `src/amiga/amiga_mesa_display.c` is CRLF, most of
  Mesa is LF. Preserve whatever the file already uses.
- The code base predates strict aliasing. Build with `-fno-strict-aliasing`;
  new code should pun through unions (`fi_type`) anyway.

## Layout

- `src/amiga/` — the driver. `amiga_mesa.c` creates the context;
  `amiga_mesa_display.c` holds the span read/write functions, clear and swap.
  The back buffer is ARGB32 in fast RAM, flipped via a row-pointer table, and
  copied to the window with `p96WritePixelArray`.
- `src/swrast/` — the rasteriser. `s_triangle.c` holds the triangle functions
  and `_swrast_choose_triangle`; `s_tritemp.h` is the scan-conversion template
  they are all built from; `s_span.c` is the generic per-span pipeline;
  `s_texture.c` the generic samplers and texture env; `s_blend.c` blending.
- `src/tnl/`, `src/math/`, `src/array_cache/`, `src/swrast_setup/` — vertex
  pipeline: immediate mode, transform, clip, and building rasteriser vertices.
- `test/hostbench/` — host/qemu benchmark, see below.

## How rendering paths are chosen

`_swrast_choose_triangle` (end of `s_triangle.c`) picks one triangle function
whenever relevant state changes. Roughly, fastest to slowest:

| Function | Selected when | Inner loop |
|---|---|---|
| `simple_textured_triangle`, `simple_z_textured_triangle` | one RGB texture, NEAREST, REPLACE/DECAL, `GL_FASTEST`, depth func LESS, nothing else enabled | integer, writes directly |
| `affine_textured_triangle` | one RGB/RGBA texture, min filter == mag filter, `GL_FASTEST` | integer, no perspective correction |
| `persp_textured_triangle` | as above with `GL_NICEST`, or filter is `*_MIPMAP_NEAREST` | integer, perspective divide every 16 pixels |
| `dualtex_triangle` (NOVA) | exactly units 0 and 1, see below | integer, divide every 16 pixels |
| `general_textured_triangle`, `multitextured_triangle` | everything else | float per pixel, texels via function pointers |

Conditions shared by the single-texture fast paths: unit 0 only, 2D,
`GL_REPEAT` both ways, no border, `MESA_FORMAT_RGB` or `RGBA`, env mode not
`GL_COMBINE`, single colour (no separate specular).

`dualtex_triangle` needs: unit 0 `GL_REPLACE` with an RGB or RGBA texture;
unit 1 an RGB texture with `GL_MODULATE`, or `GL_COMBINE` set to
MODULATE(PREVIOUS, TEXTURE) with RGB scale 1, 2 or 4; both `GL_REPEAT`, no
border, filter NEAREST, LINEAR or the matching `*_MIPMAP_NEAREST`, default LOD
bias and clamps.

Things that push an application onto the slow general path:

- trilinear filtering (`*_MIPMAP_LINEAR`), or min and mag filters that disagree
- `GL_COMBINE` on a single unit, three or more units, DECAL/ADD/BLEND on unit 1
- wrap modes other than `GL_REPEAT`, texture borders, non-RGB/RGBA formats
- blend functions other than the special-cased ones in
  `_swrast_choose_blend_func` (the rest use float `blend_general`)

To see which function a given state selects, build the benchmark with
`-DDEBUG`; `_mesa_triFuncName` holds the name.

## What has been optimised so far

On branch `claude/fitzquake-68k-perf-review-w00266`:

1. `mmath.h` — `USE_IEEE` enabled for m68k. Before, `LOG2()` was a libm
   `log()` per mipmapped pixel, `IFLOOR`/`ICEIL` were round+compare+branch, and
   `IS_INF_OR_NAN` was a `finite()` call per triangle. `LOG2` now puns through
   a union.
2. Driver — RGBA<->ARGB32 span conversion is one 32-bit load, rotate, store
   (gcc emitted 14 instructions per pixel for the byte-wise form). No software
   alpha plane: the ARGB back buffer already carries alpha.
3. `dualtex_triangle` — the integer two-unit path described above.
4. `fast_persp_span` — 16-pixel subdivision reusing the affine inner loop
   (`affine_fill`), plus `*_MIPMAP_NEAREST` with the level chosen per sub-span.
5. `span_zreject_ok` / `span_hidden` — affine, perspective and dualtex spans
   that fail the depth test everywhere are dropped before texturing. Only for
   the 16-bit software depth buffer, LESS/LEQUAL, no stencil, no clip bit.
6. `blend_modulate2x` — integer blend for `(GL_DST_COLOR, GL_SRC_COLOR)`.
7. Generic span — texcoords interpolated after the depth test when alpha test
   is off; `compute_lambda` uses two divides instead of four.
8. `MAX_TEXTURE_UNITS` reduced from 8 to 4.

Measured with the benchmark, default lightmapped state: 17.7 ms -> about 5 ms
per frame native x86-64 at 640x480; 356 ms -> about 34 ms as m68k code under
qemu at 320x240. qemu emulates the FPU in software and so overstates float
cost; Emu68 should land between those two ratios. **None of this has been
measured on real hardware yet.**

Known visual differences from stock: the subdivided paths differ slightly on
steeply angled surfaces and at mip boundaries; bilinear in the dualtex path
uses 8-bit weights; the new blend differs by at most 2/255.

Only `GL_REPLACE` on unit 0 has been exercised by the benchmark. Other env
modes run through the original affine inner loop, unchanged but unrendered in
testing.

## Benchmark and verification

`test/hostbench/` builds the whole library against a stub Picasso96 header and
renders a corridor of lightmapped quads under several state combinations
(filter mode x perspective hint x multitexture / two-pass / texture-only).

    cd test/hostbench
    ./build.sh                                              # native
    CFLAGS=-DDEBUG TAG=debug ./build.sh                     # prints triangle function names
    CC="m68k-linux-gnu-gcc -m68040" LDFLAGS=-static TAG=m68k ./build.sh
    out/bench 640 480 30
    qemu-m68k-static out/bench_m68k 320 240 2 /tmp/shot     # also dumps PPMs
    python3 cmp.py before.ppm after.ppm                     # % differing pixels

On Ubuntu the cross tools are `gcc-m68k-linux-gnu` and `qemu-user-static`.

Use it this way:

- The m68k build is big-endian and is the check that pixel packing and
  float-bit tricks are correct. Dump PPMs before and after a change and diff
  them. Behaviour-neutral changes should be byte-identical.
- Native timings understate, and qemu timings overstate, the cost of float
  code. Trust the direction, not the ratio. Pin to one core (`taskset`) and
  repeat; run-to-run noise under qemu is around 10-15%.
- `valgrind --tool=callgrind` on a `-g` native build gives a usable
  instruction-count profile.
- The scene is fill-rate bound and Quake-shaped. It says little about vertex
  or triangle-setup cost. Add scenes before optimising those (see below).

## Planned next: triangle setup and the vertex pipeline

Not started. Wait for the owner's on-hardware results from the work above; if
typical scenes are fill-rate bound, this may not be worth doing.

Steps, in order:

1. **Extend the benchmark** with workloads that are not Quake-shaped: several
   thousand small triangles, smooth-shaded and lit; untextured geometry;
   something close to the Gears example and to G-WARS. No change may make any
   of them slower.
2. **Profile that**, then fix what the profile shows. Candidates, from reading
   the code (not yet measured):
   - *Triangle setup* (`s_tritemp.h`): three float divides and many
     float->fixed conversions per triangle. A fixed-point setup, or computing
     only the gradients a given triangle function needs, should help scenes
     with many small triangles.
   - *Immediate mode* (`tnl/t_imm_*.c`): every `glVertex`/`glTexCoord` goes
     through the dispatch table into the immediate buffer, is then run through
     the pipeline, and copied again into `SWvertex` by `swrast_setup`. Look for
     a shorter route for the common case (position plus one or two texcoords,
     no lighting).
   - *Pipeline stages* (`tnl/t_pipeline.c`, `t_vb_*.c`): the fixed stage list
     is walked for every batch. Check that inactive stages are cheap.
   - *Clipping* (`tnl/t_vb_cliptmp.h`): generic, interpolates every attribute.
   - *State validation*: every `glBindTexture` sets `_NEW_TEXTURE` and forces
     the triangle and sampler functions to be re-chosen before the next
     primitive.
3. **Widen the narrow fast paths where cheap**: `GL_MODULATE` on unit 0 of the
   dualtex path (vertex-coloured lightmapped geometry), `GL_DECAL`/`GL_ADD` on
   unit 1, so it becomes a general two-texture path.

Other ideas noted but deliberately left alone:

- Writing spans straight into the back buffer with an inline 16-bit depth test
  instead of staging through `GLchan[4]` arrays. Means duplicating fog, blend
  and masking per path; wants a real profile first.
- Sharing the perspective divide between the two units in `dualtex_span` when
  their `q` is identical (it nearly always is).
- The swap always passes `RGBFB_A8R8G8B8` to `p96WritePixelArray`. If the
  screen is another 32-bit format, P96 converts every frame; rendering in the
  screen's own format would avoid that.

## Application-side notes (from the FitzQuake port)

Useful as an example of how an application's state maps onto the paths above:

- Its world pass is unit 0 REPLACE plus unit 1 lightmap, MODULATE or
  COMBINE x2 when `gl_overbright` is on — the dualtex case.
- `gl_texturemode` 0, 1, 3 and 4 stay on fast paths; 2 and 5 are trilinear.
- It sets `GL_PERSPECTIVE_CORRECTION_HINT` to `GL_FASTEST`, which selects the
  affine path for non-mipmapped single textures and visibly warps large
  polygons.
- Sky is drawn flat and then again as two blended layers unless `r_fastsky 1`.
- Alias models with `gl_overbright_models 1` use single-unit `GL_COMBINE`,
  which is still on the general path.
