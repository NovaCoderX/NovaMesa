# NovaMesa
Software implementation of OpenGL based on Mesa v4.1 for Amiga 68k RTG.

I used this library to build my Geometry Wars inspired 68k shooter, G-WARS. 

For performance reasons, only 32-bit displays are supported and the only practical target for this library is PiStorm 4.

## Performance

Stock Mesa 4.1 does most of its texturing in floating point per pixel, which can be very slow on a 68k FPU. NovaMesa adds fixed-point paths for the common cases:

- single texture, perspective correct, with nearest, bilinear or mipmap-nearest filtering
- two textures (texture on unit 0 with `GL_REPLACE`, lightmap on unit 1 with `GL_MODULATE` or a scaled `GL_COMBINE` modulate)
- spans that are completely hidden behind the depth buffer are skipped before texturing

To stay on the fast paths an application should use `GL_REPEAT` wrapping, RGB or RGBA textures without borders, and avoid trilinear filtering (`GL_*_MIPMAP_LINEAR`). Anything else still works but falls back to the original, slower code.

`GL_MAX_TEXTURE_UNITS_ARB` is 4.

## Building

Cross-compiled with gcc 13. Recommended flags: `-O2 -m68060 -noixemul -fno-strict-aliasing`.

## Host benchmark

`test/hostbench` builds the library against a stub driver so it can be run and profiled on a PC, or as m68k code under qemu. See `test/hostbench/build.sh` and `CLAUDE.md` for details.
