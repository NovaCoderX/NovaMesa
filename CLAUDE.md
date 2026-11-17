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
- **Prefer fixed point in inner loops.** Float divides and float<->int
  conversions are expensive on every target, and under a software-emulated
  FPU (qemu, FS-UAE without a JIT FPU) all float work is. Float work per
  triangle or per 16-pixel run is fine; float work per pixel is not. But do
  not convert per-vertex or per-triangle float maths to fixed point just for
  its own sake: on a real 68060 and on Emu68 a float add or multiply costs
  about the same as an integer one.
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
| `dualtex_triangle` (NOVA) | exactly units 0 and 1, see below | integer, divide every 16 pixels; a magnified bilinear unit 1 is stepped, not filtered per pixel |
| `general_textured_triangle`, `multitextured_triangle` | everything else | float per pixel, texels via function pointers |

Conditions shared by the single-texture fast paths: unit 0 only, 2D,
`GL_REPEAT` both ways, no border, `MESA_FORMAT_RGB` or `RGBA`, env mode not
`GL_COMBINE`, single colour (no separate specular).

`dualtex_triangle` needs either of these, with both textures `GL_REPEAT`, no
border, filter NEAREST, LINEAR or the matching `*_MIPMAP_NEAREST`, default LOD
bias and clamps:

- texture times lightmap: unit 0 `GL_REPLACE` with an RGB or RGBA texture;
  unit 1 an RGB texture with `GL_MODULATE`, or `GL_COMBINE` set to
  MODULATE(PREVIOUS, TEXTURE) with RGB scale 1, 2 or 4;
- decal: unit 1 an RGBA texture with `GL_DECAL`; unit 0 `GL_REPLACE`,
  `GL_MODULATE`, or `GL_COMBINE` set to TEXTURE * PRIMARY_COLOR with RGB scale
  1, 2 or 4.

Things that push an application onto the slow general path:

- trilinear filtering (`*_MIPMAP_LINEAR`), or min and mag filters that disagree
- `GL_COMBINE` on a single unit other than modulate with an RGB scale (fifth
  round), three or more units, ADD/BLEND on unit 1, DECAL on unit 1 with an
  RGB texture
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
cost; Emu68 should land between those two ratios. The owner measured
FitzQuake `timedemo` in FS-UAE going from about 10 to 26.3 FPS with this
round; nothing has been measured on a real PiStorm yet.

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
    out/bench 640 480 30                                    # fill-rate bound corridor
    out/geom 640 480 30                                     # geometry-bound workloads
    qemu-m68k-static out/bench_m68k 320 240 2 /tmp/shot     # also dumps PPMs
    python3 cmp.py before.ppm after.ppm                     # % differing pixels

On Ubuntu the cross tools are `gcc-m68k-linux-gnu` and `qemu-user-static`.

Use it this way:

- `build.sh` deletes each object before compiling it, so a source file that
  fails to compile fails the link. (It used to link the previous object and
  report success.)
- The m68k build is big-endian and is the check that pixel packing and
  float-bit tricks are correct. Dump PPMs before and after a change and diff
  them. Behaviour-neutral changes should be byte-identical.
- Native timings understate, and qemu timings overstate, the cost of float
  code. Trust the direction, not the ratio. Pin to one core (`taskset`) and
  repeat; run-to-run noise under qemu is around 10-15%.
- `valgrind --tool=callgrind` on a `-g` native build gives a usable
  instruction-count profile.
- `bench` is fill-rate bound and Quake-shaped; `geom` covers many small
  triangles, lit and untextured geometry, lines and 2D. Run both: a change
  must not make either slower.

## Second round: small triangles and geometry (done)

`test/hostbench/geom.c` was added for geometry-bound workloads: gears (display
lists, lit), 24 tessellated spheres (textured with colour per vertex; GL lit;
flat), 4000 additive 2D lines, 3000 blended textured 2D quads.

What profiling it showed:

- **The vertex pipeline is not the problem.** Immediate mode, transform, clip
  test, lighting and vertex emit together were under 10% of instructions in
  every workload. No work was done there.
- **Triangle setup** (`s_tritemp.h`) was 30-35% for small triangles, with no
  hot line: it is dozens of float->fixed conversions and gradient products
  spread evenly.
- **The generic span pipeline** was another third: with about 1.4 spans per
  triangle, the fixed overhead of `_mesa_write_rgba_span` (mask fill, colour
  and Z arrays, separate depth pass, driver call) dominates the pixels.
- Small textured triangles additionally paid two float divides per span in
  the perspective path.

What was done about it:

1. `IROUND` on m68k is a single `fmove.l` (inline asm in `mmath.h`) instead of
   test + branch + add + `fintrz`. `FloatToFixed` uses it everywhere. Relies on
   the default round-to-nearest FPCR mode.
2. The driver supplies its own untextured triangle functions
   (`amesa_choose_triangle` in `amiga_mesa_display.c`): flat and smooth, with
   depth LESS, LEQUAL or no depth test, writing ARGB32 and Z directly. Chosen
   only when `_RasterMask` is exactly `DEPTH_BIT` or 0; everything else defers
   to `_swrast_choose_triangle`. Output is bit-identical to swrast's.
   Gotcha: test `ctx->Color._DrawDestMask == BACK_LEFT_BIT`, not
   `Color.DrawBuffer`, which is `GL_BACK` by default.
3. `persp_textured_triangle` is now a front end. If 1/w is the same at all
   three vertices (any orthographic/2D drawing), or varies so little that the
   affine error is under about half a pixel
   (`(wMax - wMin) * extent < 2 * wMin`), it calls
   `affine_mip_textured_triangle`, whose spans use no floating point; the mip
   level is chosen once per triangle. Otherwise `persp_textured_triangle_full`.
4. `fast_persp_span` does a span in one run when q does not change along it.

Measured as m68k code under qemu, 320x240: gears 7.1 -> 4.8 ms, textured
spheres 160 -> 110, lit spheres 81 -> 67, flat spheres 54 -> 45, blended 2D
quads 205 -> 93. Lines unchanged. The untextured results are bit-identical;
the textured ones differ in under 1% of pixels.

## Third round: lines, points, per-vertex colour (done)

Prompted by looking at G-WARS, which is nothing like Quake: 2D, no depth test,
no blending, no textures; thousands of width-1 `GL_LINES` and `GL_POINTS` per
frame via `glVertex2i` with one `glColor3fv` per primitive, default (smooth)
shade model. Two workloads modelled on it were added to `geom`: "2D plain
lines + points" and "3D wireframe, depth tested".

1. **Direct lines** in the driver (`amesa_choose_line`): flat and smooth,
   width 1, unstippled, untextured, with depth LESS/LEQUAL or none, plotted
   straight into the buffer via `s_linetemp.h`. A "smooth" line whose two
   vertex colours are equal is drawn by the flat function.
   Do not use `CLIP_HACK`: it moves the end point of a line clipped at the
   right/top edge and so shifts the whole line. Instead any line with an end
   point on `x == width` or `y == height` is passed to swrast's own function,
   saved in `a_ctx->swrast_line` when the line function is chosen.
2. **Direct size-1 points** (`amesa_choose_point`), bounds-checked for the
   same edge case.
3. `amesa_direct_mode()` is the shared "is the state simple enough" test for
   the driver's triangle, line and point functions.
4. **Float colour -> byte conversion** (`trans_4_GLfloat_4ub_raw` and the
   3-component one in `math/m_translate.c`) is hand-written: if a vertex's
   colour has the same bit pattern as the previous vertex's, the previous
   result is copied. This was 11% of the plain-lines workload.
5. **Triangle setup** (`s_tritemp.h`, `smoothRGBA`): with smooth shading and
   three equal vertex colours the colour gradients are skipped. `smoothBias`
   keeps the result bit-identical to the smooth path.
6. `triangle_is_near_affine()` factored out of the perspective front end.

Everything in this round is **byte-identical** to the previous output, in
both benchmarks, natively and as m68k code.

Native instruction counts (deterministic, init excluded): plain 2D lines and
points 31.7M -> 15.6M, depth-tested wireframe 18.5M -> 13.1M, lit spheres
-4%, blended 2D quads -2%. Textured spheres with a different colour at every
vertex got 1.5% *slower* (the extra colour compares buy nothing there). Under
qemu the wall-clock gains are much smaller (about 5-10%) because its software
FPU makes the per-vertex float work dominate lines.

Tried and reverted: applying the near-affine test to `dualtex_span` (whole
span in one run). No measurable gain in the corridor and it changed about 1%
of pixels, so it was not kept.

Measuring: wall-clock under qemu on a shared machine was too noisy this round
(the same binary varied by 30%). Use
`valgrind --tool=callgrind --callgrind-out-file=/dev/null <bench>` and read
the "Collected" instruction count for regressions, subtracting an init-only
run (`ONLY=99`), and take the best of several qemu runs for float-heavy
direction.

## Fourth round: four candidates evaluated

The owner picked four of the remaining ideas to try and keep only if they
proved worth it. One was implemented; three were measured and declined. The
measurements are recorded so they are not repeated.

**Blended primitives -- implemented.** swrast blended by reading the
destination span into a temporary through the driver, blending array against
array, and writing back: three passes and two pixel-format conversions. There
are now two optional hooks in `struct swrast_device_driver` (`swrast.h`),
`BlendRGBASpan` and `BlendRGBAPixels`, called from `driver_blend_span()` in
`s_span.c` for both the plain and the textured span writers. The driver
(`blend_rgba_span` / `blend_rgba_pixels` in `amiga_mesa_display.c`) blends
against the ARGB32 words in one loop for the modes swrast special-cases:
transparency, add, modulate and 2x modulate. Any other mode returns GL_FALSE
and swrast does it the old way. The formulas are copied from `s_blend.c`, so
the output is byte-identical. Native instruction counts: additive lines -21%,
blended textured quads -17%, two-pass lightmapped corridor -9%. About -5% and
-11% wall-clock for the first two under qemu.

Also: the driver lines' edge check moved into `s_linetemp.h`'s `SETUP_CODE`,
where the integer end points already exist, saving four float->int
conversions per line.

**Vertex pipeline fast path -- declined.** For the 2D lines-and-points
workload the whole vertex side (immediate API, fixup, transform, clip test,
building `SWvertex`) is about 30% of native instructions, roughly 180 per
vertex, spread across six stages at 10-30 each with no hot spot. Under qemu's
software FPU it is about half the frame. A fast path would mean a second
vertex pipeline for simple state and at best halve that 30%.

**Triangle setup in fixed point / early cull -- declined.** An experiment
culling on the unsnapped float area before the fixed-point conversions saved
1.5-4.5% of instructions on the sphere workloads but changed a few silhouette
pixels (the area sign of sub-pixel slivers can differ after snapping); making
it exact needs a conservative margin that costs most of the gain. Fixed-point
colour gradients need 64-bit products to be safe on sliver triangles (which
`-m68060` turns into library calls), lose bit-exactness, and target at most
about 5% on smooth-shaded small triangles.

**State validation on glBindTexture -- declined.** Measured with the two new
`geom` workloads ("2D textured quads, one texture" vs "bind per quad"): about
2,400 native instructions per bind-plus-revalidation, including flushing the
immediate buffer, with no single expensive step. A few hundred binds a frame
is well under 1% of a frame.

## Fifth round: driven by a real FitzQuake profile

FitzQuake now has a host harness (`FitzQuake/test/hostbench/`, see its
`CLAUDE.md`) that builds the whole engine against this library and runs the
shareware `timedemo`s under callgrind. That replaced guessing with a profile
of a real application: 96.5% of a frame is in this library, 41% in one loop
(`DUALTEX_LOOP(NEAREST, LINEAR)`, the bilinear lightmap), and the generic span
code after it is another 13.5%. Use that harness as well as `bench` and `geom`
for anything that touches the textured paths; it can hash every frame of a
demo, which is a much stronger identity check than a few screenshots.

Done, measured on `timedemo demo1` at 320x240 (x86-32 instruction counts):

1. **Lightmap stepping in `dualtex_span`** (`DUALTEX_LOOP_LMCELL`). When unit 1
   is bilinear and moves at most a quarter texel per pixel, the filter is not
   evaluated per pixel. Between the same four texels the bilinear result is an
   exact quadratic in the pixel index, so it is stepped with a first and a
   second difference per channel and set up again only when the scanline
   enters another texel. 29.77 G -> 27.08 G (-9.0%); corridor benchmark -15%.
   Differs from the per-pixel filter only in rounding (that truncates after
   each of three lerps): 13.6% of pixels by 1/255, 0.005% by 2, none by more
   over 75 frames of three demos; `bench` up to 4 with x2 overbright on its
   random lightmap. `geom` byte-identical.
   Two approximate versions were tried first and rejected, do not go back to
   them: filtering every 2/4/8 pixels and interpolating linearly was as fast
   but went wrong by up to 25/255 where a dynamic light makes the lightmap
   change sharply inside one texel; falling back to per-pixel whenever a run
   crossed a texel fixed that and kept only 1.2% of the gain.
   The products are sized for `FIXED_SHIFT == 11` and a step of at most a
   quarter texel; the path is switched off for `FIXED_14`.
2. **Direct flat blended triangles in the driver** (`flat_blend_*_triangle`,
   chosen in `amesa_choose_triangle` when `_RasterMask == BLEND_BIT` and
   shading is flat). Triangles above about 4000 pixels use four 256-entry
   tables built by calling the blend function itself, kept until the colour
   or mode changes. Quake's full-screen tint was about 100 instructions a
   pixel. 27.08 G -> 26.33 G (-2.8%); byte-identical everywhere.
3. `is_fan_like[]` in `tnl/t_imm_fixup.c` was indexed one to three entries
   past its end outside `glBegin` (old Mesa bug, found with AddressSanitizer).
4. **Single-unit `GL_COMBINE` as scaled modulate.** `combine_is_scaled_modulate()`
   accepts RGB = TEXTURE * PRIMARY (or PREVIOUS) with scale 1, 2 or 4 and the
   default alpha combine; the affine and perspective paths then run it as
   `GL_MODULATE` with a shift and a clamp (`MODULATE_SCALED` in `affine_fill`,
   `AFFINE_ENV_MODULATE_2X/4X` in `affine_info.envmode`). This is FitzQuake's
   overbright alias model without a fullbright mask, which used to be on
   `general_textured_triangle`. 26.33 G -> 26.13 G (-0.8%) on x86, where the
   float path is cheap; expect more on a 68k and much more under a software
   FPU. Not identical: like the other fast paths it picks a neighbouring
   texel here and there (0.5% of pixels differ, 0.05% by more than 8/255, all
   on the models). `bench` and `geom` are byte-identical.

5. **`GL_DECAL` on unit 1 in the dualtex path** (`info->decal`,
   `DUALTEX_LOOP_DECAL`). Unit 1 must be an RGBA texture; unit 0 may be
   `GL_REPLACE`, `GL_MODULATE`, or the scaled-modulate combine from item 4
   (`info->mod0Shift`), in which case the vertex colour is interpolated in the
   span. Any of the four nearest/linear combinations, mipmapped or not. The
   arithmetic is the general path's (`CHAN_PRODUCT` for the decal). This takes
   two FitzQuake cases off `multitextured_triangle`: alias models with a
   fullbright mask (26.13 G -> 25.71 G on demo1, -1.6%; the view weapon -27%)
   and the sky's two cloud layers (`Sky_DrawSky` -19%, a view that is half sky
   -10%). About 0.1% of pixels differ from the general path, texel choice at
   edges; `bench` and `geom` are byte-identical.
   The sky is still dear for what it is: several hundred small quads a frame,
   each paying dualtex's float setup, drawn with depth func GEQUAL so the
   span reject does not apply.

Together demo1 -13.6% (29.77 G -> 25.71 G); the first two alone were demo1
-11.6%, demo2 -13.2%. Not run as m68k code this round: the
cross compiler could not be installed in the session. Neither change depends
on byte order (bytes and whole ARGB words only), but run the qemu check
before relying on that.

Next candidates from the same profile, best first:

- **A dualtex variant that tests and writes Z and ARGB itself** for the plain
  world state (depth LESS/LEQUAL with writes, no blend, alpha test, fog or
  stencil). After texturing, every span still goes through
  `_mesa_span_interpolate_z`, `depth_test_span`, `_mesa_write_rgba_span` and
  the driver's `write_rgba_span`: about 13.5% of a frame. Needs a way for
  swrast to reach the driver's rows, like the blend hooks.

## Ideas not done yet

- **Direct blended lines and points in the driver.** Flat blended triangles
  are done (fifth round). Blended lines and points, and smooth shaded blended
  triangles, still go through swrast's generic functions and span pipeline
  first. Only worth it for an application that draws a lot of them; none of
  the known ones do.
- **Wide lines** (`glLineWidth` > 1) go through the general path.
- **Widen the dualtex path further**: `GL_MODULATE` on unit 0 with a
  modulating unit 1, `GL_ADD` on unit 1. (`GL_DECAL` on unit 1 is done.)
- **Clear and swap.** `glClear` copies a pre-filled buffer the size of the
  screen (8% of the 2D lines workload) and the swap copies it again; a fill
  loop and rendering in the screen's native format would cut memory traffic.
  Needs measuring on real hardware, not here.

Other ideas noted but deliberately left alone:

- Direct-to-buffer writes for the *textured* paths. Means duplicating fog,
  blend and masking per path.
- Sharing the perspective divide between the two units in `dualtex_span` when
  their `q` is identical (it nearly always is).

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

G-WARS, for contrast, uses only unblended 2D lines, points and a few quads
with no depth test or textures (see the third round above).
