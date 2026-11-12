/* $Id: s_triangle.c,v 1.63 2002/10/24 23:57:24 brianp Exp $ */

/*
 * Mesa 3-D graphics library
 * Version:  4.1
 *
 * Copyright (C) 1999-2002  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


/*
 * When the device driver doesn't implement triangle rasterization it
 * can hook in _swrast_Triangle, which eventually calls one of these
 * functions to draw triangles.
 */

#include "glheader.h"
#include "context.h"
#include "colormac.h"
#include "imports.h"
#include "macros.h"
#include "mmath.h"
#include "texformat.h"
#include "teximage.h"
#include "texstate.h"

#include "s_aatriangle.h"
#include "s_context.h"
#include "s_depth.h"
#include "s_feedback.h"
#include "s_span.h"
#include "s_triangle.h"


/*
 * Just used for feedback mode.
 */
GLboolean _mesa_cull_triangle( GLcontext *ctx,
			    const SWvertex *v0,
			    const SWvertex *v1,
			    const SWvertex *v2 )
{
   GLfloat ex = v1->win[0] - v0->win[0];
   GLfloat ey = v1->win[1] - v0->win[1];
   GLfloat fx = v2->win[0] - v0->win[0];
   GLfloat fy = v2->win[1] - v0->win[1];
   GLfloat c = ex*fy-ey*fx;

   if (c * SWRAST_CONTEXT(ctx)->_backface_sign > 0)
      return 0;

   return 1;
}



/*
 * Render a flat-shaded color index triangle.
 */
static void flat_ci_triangle( GLcontext *ctx,
			      const SWvertex *v0,
			      const SWvertex *v1,
			      const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1

#define SETUP_CODE					\
   span.interpMask |= SPAN_INDEX;			\
   span.index = IntToFixed(v2->index);			\
   span.indexStep = 0;

#define RENDER_SPAN( span )  _mesa_write_index_span(ctx, &span);

#include "s_tritemp.h"
}



/*
 * Render a smooth-shaded color index triangle.
 */
static void smooth_ci_triangle( GLcontext *ctx,
				const SWvertex *v0,
				const SWvertex *v1,
				const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define INTERP_INDEX 1

#define RENDER_SPAN( span )  _mesa_write_index_span(ctx, &span);

#include "s_tritemp.h"
}



/*
 * Render a flat-shaded RGBA triangle.
 */
static void flat_rgba_triangle( GLcontext *ctx,
				const SWvertex *v0,
				const SWvertex *v1,
				const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE

#define SETUP_CODE				\
   ASSERT(ctx->Texture._EnabledUnits == 0);	\
   ASSERT(ctx->Light.ShadeModel==GL_FLAT);	\
   span.interpMask |= SPAN_RGBA;		\
   span.red = ChanToFixed(v2->color[0]);	\
   span.green = ChanToFixed(v2->color[1]);	\
   span.blue = ChanToFixed(v2->color[2]);	\
   span.alpha = ChanToFixed(v2->color[3]);	\
   span.redStep = 0;				\
   span.greenStep = 0;				\
   span.blueStep = 0;				\
   span.alphaStep = 0;

#define RENDER_SPAN( span )  _mesa_write_rgba_span(ctx, &span);

#include "s_tritemp.h"
}



/*
 * Render a smooth-shaded RGBA triangle.
 */
static void smooth_rgba_triangle( GLcontext *ctx,
				  const SWvertex *v0,
				  const SWvertex *v1,
				  const SWvertex *v2 )
{

#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_ALPHA 1

#define SETUP_CODE				\
   {						\
      /* texturing must be off */		\
      ASSERT(ctx->Texture._EnabledUnits == 0);	\
      ASSERT(ctx->Light.ShadeModel==GL_SMOOTH);	\
   }

#define RENDER_SPAN( span )  _mesa_write_rgba_span(ctx, &span);

#include "s_tritemp.h"

}


/*
 * Render an RGB, GL_DECAL, textured triangle.
 * Interpolate S,T only w/out mipmapping or perspective correction.
 *
 * No fog.
 */
static void simple_textured_triangle( GLcontext *ctx,
				      const SWvertex *v0,
				      const SWvertex *v1,
				      const SWvertex *v2 )
{
#define INTERP_INT_TEX 1
#define S_SCALE twidth
#define T_SCALE theight

#define SETUP_CODE							\
   SWcontext *swrast = SWRAST_CONTEXT(ctx);                             \
   struct gl_texture_object *obj = ctx->Texture.Unit[0].Current2D;	\
   const GLint b = obj->BaseLevel;					\
   const GLfloat twidth = (GLfloat) obj->Image[b]->Width;		\
   const GLfloat theight = (GLfloat) obj->Image[b]->Height;		\
   const GLint twidth_log2 = obj->Image[b]->WidthLog2;			\
   const GLchan *texture = (const GLchan *) obj->Image[b]->Data;	\
   const GLint smask = obj->Image[b]->Width - 1;			\
   const GLint tmask = obj->Image[b]->Height - 1;			\
   if (!texture) {							\
      /* this shouldn't happen */					\
      return;								\
   }

#define RENDER_SPAN( span  )						\
   GLuint i;								\
   span.intTex[0] -= FIXED_HALF; /* off-by-one error? */		\
   span.intTex[1] -= FIXED_HALF;					\
   for (i = 0; i < span.end; i++) {					\
      GLint s = FixedToInt(span.intTex[0]) & smask;			\
      GLint t = FixedToInt(span.intTex[1]) & tmask;			\
      GLint pos = (t << twidth_log2) + s;				\
      pos = pos + pos + pos;  /* multiply by 3 */			\
      span.array->rgb[i][RCOMP] = texture[pos];				\
      span.array->rgb[i][GCOMP] = texture[pos+1];			\
      span.array->rgb[i][BCOMP] = texture[pos+2];			\
      span.intTex[0] += span.intTexStep[0];				\
      span.intTex[1] += span.intTexStep[1];				\
   }									\
   (*swrast->Driver.WriteRGBSpan)(ctx, span.end, span.x, span.y,	\
                                  (CONST GLchan (*)[3]) span.array->rgb,\
                                  NULL );

#include "s_tritemp.h"
}


/*
 * Render an RGB, GL_DECAL, textured triangle.
 * Interpolate S,T, GL_LESS depth test, w/out mipmapping or
 * perspective correction.
 *
 * No fog.
 */
static void simple_z_textured_triangle( GLcontext *ctx,
					const SWvertex *v0,
					const SWvertex *v1,
					const SWvertex *v2 )
{
#define INTERP_Z 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_INT_TEX 1
#define S_SCALE twidth
#define T_SCALE theight

#define SETUP_CODE							\
   SWcontext *swrast = SWRAST_CONTEXT(ctx);                             \
   struct gl_texture_object *obj = ctx->Texture.Unit[0].Current2D;	\
   const GLint b = obj->BaseLevel;					\
   const GLfloat twidth = (GLfloat) obj->Image[b]->Width;		\
   const GLfloat theight = (GLfloat) obj->Image[b]->Height;		\
   const GLint twidth_log2 = obj->Image[b]->WidthLog2;			\
   const GLchan *texture = (const GLchan *) obj->Image[b]->Data;	\
   const GLint smask = obj->Image[b]->Width - 1;			\
   const GLint tmask = obj->Image[b]->Height - 1;			\
   if (!texture) {							\
      /* this shouldn't happen */					\
      return;								\
   }

#define RENDER_SPAN( span )						\
   GLuint i;				    				\
   span.intTex[0] -= FIXED_HALF; /* off-by-one error? */		\
   span.intTex[1] -= FIXED_HALF;					\
   for (i = 0; i < span.end; i++) {					\
      const GLdepth z = FixedToDepth(span.z);				\
      if (z < zRow[i]) {						\
         GLint s = FixedToInt(span.intTex[0]) & smask;			\
         GLint t = FixedToInt(span.intTex[1]) & tmask;			\
         GLint pos = (t << twidth_log2) + s;				\
         pos = pos + pos + pos;  /* multiply by 3 */			\
         span.array->rgb[i][RCOMP] = texture[pos];			\
         span.array->rgb[i][GCOMP] = texture[pos+1];			\
         span.array->rgb[i][BCOMP] = texture[pos+2];			\
         zRow[i] = z;							\
         span.array->mask[i] = 1;					\
      }									\
      else {								\
         span.array->mask[i] = 0;					\
      }									\
      span.intTex[0] += span.intTexStep[0];				\
      span.intTex[1] += span.intTexStep[1];				\
      span.z += span.zStep;						\
   }									\
   (*swrast->Driver.WriteRGBSpan)(ctx, span.end, span.x, span.y,	\
                                  (CONST GLchan (*)[3]) span.array->rgb,\
                                  span.array->mask );

#include "s_tritemp.h"
}


#if CHAN_TYPE != GL_FLOAT

/*
 * NOVA: the textured fast paths below fill in the colour array and only then
 * hand the span to _mesa_write_rgba_span() for the depth test, so hidden
 * fragments used to be textured and then thrown away.  With Quake-style
 * unsorted geometry that is a large share of all fragments.  These two helpers
 * let a span function discard a fully hidden span before doing any work.
 *
 * It is only safe when nothing else depends on the span being processed
 * (stencil, clipping) and the depth buffer is our own 16-bit software one.
 */
static INLINE GLboolean
span_zreject_ok( const GLcontext *ctx )
{
   const SWcontext *swrast = SWRAST_CONTEXT(ctx);
   return (GLboolean) (ctx->Depth.Test
                       && (ctx->Depth.Func == GL_LESS
                           || ctx->Depth.Func == GL_LEQUAL)
                       && ctx->Visual.depthBits <= 16
                       && ctx->DrawBuffer->DepthBuffer
                       && !swrast->Driver.ReadDepthSpan
                       && !ctx->Stencil.Enabled
                       && !(swrast->_RasterMask & CLIP_BIT));
}

/*
 * Would every fragment of this span fail the depth test?
 * Mirrors _mesa_span_interpolate_z() + depth_test_span16(), without writing.
 */
static INLINE GLboolean
span_hidden( const GLcontext *ctx, const struct sw_span *span )
{
   const GLushort *zbuf = (const GLushort *) ctx->DrawBuffer->DepthBuffer
                        + ctx->DrawBuffer->Width * span->y + span->x;
   const GLfixed zStep = span->zStep;
   GLfixed z = span->z;
   GLuint i;

   if (ctx->Depth.Func == GL_LESS) {
      for (i = 0; i < span->end; i++) {
         if ((GLdepth) FixedToInt(z) < (GLdepth) zbuf[i])
            return GL_FALSE;
         z += zStep;
      }
   }
   else {
      for (i = 0; i < span->end; i++) {
         if ((GLdepth) FixedToInt(z) <= (GLdepth) zbuf[i])
            return GL_FALSE;
         z += zStep;
      }
   }
   return GL_TRUE;
}


/*
 * NOVA: is perspective correction worth doing for this triangle?
 *
 * It only matters when 1/w differs noticeably between the vertices.  For
 * 2D/orthographic drawing it is constant, and for small or distant triangles
 * it barely changes; in both cases plain linear interpolation lands within a
 * fraction of a pixel of the correct result.  This bounds the screen-space
 * error (roughly a quarter of the relative change in w times the triangle's
 * size) to about half a pixel.
 */
static INLINE GLboolean
triangle_is_near_affine( const SWvertex *v0, const SWvertex *v1,
                         const SWvertex *v2 )
{
   const GLfloat w0 = v0->win[3], w1 = v1->win[3], w2 = v2->win[3];
   GLfloat wMin = w0, wMax = w0;

   if (w1 < wMin) wMin = w1; else if (w1 > wMax) wMax = w1;
   if (w2 < wMin) wMin = w2; else if (w2 > wMax) wMax = w2;

   if (wMin <= 0.0F)
      return GL_FALSE;
   if (wMin == wMax)
      return GL_TRUE;
   {
      /* largest extent of the triangle on screen, in pixels */
      const GLfloat ax = FABSF(v1->win[0] - v0->win[0]);
      const GLfloat bx = FABSF(v2->win[0] - v0->win[0]);
      const GLfloat cx = FABSF(v2->win[0] - v1->win[0]);
      const GLfloat ay = FABSF(v1->win[1] - v0->win[1]);
      const GLfloat by = FABSF(v2->win[1] - v0->win[1]);
      const GLfloat cy = FABSF(v2->win[1] - v1->win[1]);
      GLfloat extent = MAX2(ax, bx);
      extent = MAX2(extent, cx);
      extent = MAX2(extent, ay);
      extent = MAX2(extent, by);
      extent = MAX2(extent, cy);
      /* (wMax/wMin - 1) * extent / 4 < 0.5 pixel */
      return (GLboolean) ((wMax - wMin) * extent < 2.0F * wMin);
   }
}


struct affine_info
{
   GLenum filter;
   GLenum format;
   GLenum envmode;
   GLint smask, tmask;
   GLint twidth_log2;
   const GLchan *texture;
   GLfixed er, eg, eb, ea;
   GLint tbytesline, tsize;
   /* NOVA: */
   GLboolean zReject;                     /* see span_zreject_ok() */
   const struct gl_texture_object *obj;   /* perspective path only */
   GLint maxLevel;                        /* perspective path only: highest
                                           * mip level usable above BaseLevel */
   GLint level;                           /* affine_mip_span(): level chosen
                                           * for this triangle, -1 = not yet */
};


/* This function can handle GL_NEAREST or GL_LINEAR sampling of 2D RGB or RGBA
 * textures with GL_REPLACE, GL_MODULATE, GL_BLEND, GL_DECAL or GL_ADD
 * texture env modes.
 */
/* NOVA: this is the body of the original affine_span(), split out so the
 * perspective path can run it over short sub-spans as well.  It textures 'n'
 * fragments into 'dest', stepping span->intTex and the span colours.
 */
static INLINE void
affine_fill(struct sw_span *span, const struct affine_info *info,
            GLchan *dest, const GLuint n)
{
   GLchan sample[4];  /* the filtered texture sample */

   /* Instead of defining a function for each mode, a test is done
    * between the outer and inner loops. This is to reduce code size
    * and complexity. Observe that an optimizing compiler kills
    * unused variables (for instance tf,sf,ti,si in case of GL_NEAREST).
    */

#define NEAREST_RGB			\
   sample[RCOMP] = tex00[RCOMP];	\
   sample[GCOMP] = tex00[GCOMP];	\
   sample[BCOMP] = tex00[BCOMP];	\
   sample[ACOMP] = CHAN_MAX

#define LINEAR_RGB							\
   sample[RCOMP] = (ti * (si * tex00[0] + sf * tex01[0]) +		\
             tf * (si * tex10[0] + sf * tex11[0])) >> 2 * FIXED_SHIFT;	\
   sample[GCOMP] = (ti * (si * tex00[1] + sf * tex01[1]) +		\
             tf * (si * tex10[1] + sf * tex11[1])) >> 2 * FIXED_SHIFT;	\
   sample[BCOMP] = (ti * (si * tex00[2] + sf * tex01[2]) +		\
             tf * (si * tex10[2] + sf * tex11[2])) >> 2 * FIXED_SHIFT;	\
   sample[ACOMP] = CHAN_MAX

#define NEAREST_RGBA  COPY_CHAN4(sample, tex00)

#define LINEAR_RGBA							\
   sample[RCOMP] = (ti * (si * tex00[0] + sf * tex01[0]) +		\
               tf * (si * tex10[0] + sf * tex11[0])) >> 2 * FIXED_SHIFT;\
   sample[GCOMP] = (ti * (si * tex00[1] + sf * tex01[1]) +		\
               tf * (si * tex10[1] + sf * tex11[1])) >> 2 * FIXED_SHIFT;\
   sample[BCOMP] = (ti * (si * tex00[2] + sf * tex01[2]) +		\
               tf * (si * tex10[2] + sf * tex11[2])) >> 2 * FIXED_SHIFT;\
   sample[ACOMP] = (ti * (si * tex00[3] + sf * tex01[3]) +		\
               tf * (si * tex10[3] + sf * tex11[3])) >> 2 * FIXED_SHIFT

#define MODULATE							  \
   dest[RCOMP] = span->red   * (sample[RCOMP] + 1u) >> (FIXED_SHIFT + 8); \
   dest[GCOMP] = span->green * (sample[GCOMP] + 1u) >> (FIXED_SHIFT + 8); \
   dest[BCOMP] = span->blue  * (sample[BCOMP] + 1u) >> (FIXED_SHIFT + 8); \
   dest[ACOMP] = span->alpha * (sample[ACOMP] + 1u) >> (FIXED_SHIFT + 8)

#define DECAL								\
   dest[RCOMP] = ((CHAN_MAX - sample[ACOMP]) * span->red +		\
               ((sample[ACOMP] + 1) * sample[RCOMP] << FIXED_SHIFT))	\
               >> (FIXED_SHIFT + 8);					\
   dest[GCOMP] = ((CHAN_MAX - sample[ACOMP]) * span->green +		\
               ((sample[ACOMP] + 1) * sample[GCOMP] << FIXED_SHIFT))	\
               >> (FIXED_SHIFT + 8);					\
   dest[BCOMP] = ((CHAN_MAX - sample[ACOMP]) * span->blue +		\
               ((sample[ACOMP] + 1) * sample[BCOMP] << FIXED_SHIFT))	\
               >> (FIXED_SHIFT + 8);					\
   dest[ACOMP] = FixedToInt(span->alpha)

#define BLEND								\
   dest[RCOMP] = ((CHAN_MAX - sample[RCOMP]) * span->red		\
               + (sample[RCOMP] + 1) * info->er) >> (FIXED_SHIFT + 8);	\
   dest[GCOMP] = ((CHAN_MAX - sample[GCOMP]) * span->green		\
               + (sample[GCOMP] + 1) * info->eg) >> (FIXED_SHIFT + 8);	\
   dest[BCOMP] = ((CHAN_MAX - sample[BCOMP]) * span->blue		\
               + (sample[BCOMP] + 1) * info->eb) >> (FIXED_SHIFT + 8);	\
   dest[ACOMP] = span->alpha * (sample[ACOMP] + 1) >> (FIXED_SHIFT + 8)

#define REPLACE  COPY_CHAN4(dest, sample)

#define ADD								\
   {									\
      GLint rSum = FixedToInt(span->red)   + (GLint) sample[RCOMP];	\
      GLint gSum = FixedToInt(span->green) + (GLint) sample[GCOMP];	\
      GLint bSum = FixedToInt(span->blue)  + (GLint) sample[BCOMP];	\
      dest[RCOMP] = MIN2(rSum, CHAN_MAX);				\
      dest[GCOMP] = MIN2(gSum, CHAN_MAX);				\
      dest[BCOMP] = MIN2(bSum, CHAN_MAX);				\
      dest[ACOMP] = span->alpha * (sample[ACOMP] + 1) >> (FIXED_SHIFT + 8); \
  }

/* shortcuts */

#define NEAREST_RGB_REPLACE		\
   NEAREST_RGB;				\
   dest[0] = sample[0];			\
   dest[1] = sample[1];			\
   dest[2] = sample[2];			\
   dest[3] = FixedToInt(span->alpha);

#define NEAREST_RGBA_REPLACE  COPY_CHAN4(dest, tex00)

#define SPAN_NEAREST(DO_TEX,COMP)					\
	for (i = 0; i < n; i++)       {				\
           /* Isn't it necessary to use FixedFloor below?? */		\
           GLint s = FixedToInt(span->intTex[0]) & info->smask;		\
           GLint t = FixedToInt(span->intTex[1]) & info->tmask;		\
           GLint pos = (t << info->twidth_log2) + s;			\
           const GLchan *tex00 = info->texture + COMP * pos;		\
           DO_TEX;							\
           span->red += span->redStep;					\
	   span->green += span->greenStep;				\
           span->blue += span->blueStep;				\
	   span->alpha += span->alphaStep;				\
	   span->intTex[0] += span->intTexStep[0];			\
	   span->intTex[1] += span->intTexStep[1];			\
           dest += 4;							\
	}

#define SPAN_LINEAR(DO_TEX,COMP)					\
	for (i = 0; i < n; i++)       {				\
           /* Isn't it necessary to use FixedFloor below?? */		\
           GLint s = FixedToInt(span->intTex[0]) & info->smask;		\
           GLint t = FixedToInt(span->intTex[1]) & info->tmask;		\
           GLfixed sf = span->intTex[0] & FIXED_FRAC_MASK;		\
           GLfixed tf = span->intTex[1] & FIXED_FRAC_MASK;		\
           GLfixed si = FIXED_FRAC_MASK - sf;				\
           GLfixed ti = FIXED_FRAC_MASK - tf;				\
           GLint pos = (t << info->twidth_log2) + s;			\
           const GLchan *tex00 = info->texture + COMP * pos;		\
           const GLchan *tex10 = tex00 + info->tbytesline;		\
           const GLchan *tex01 = tex00 + COMP;				\
           const GLchan *tex11 = tex10 + COMP;				\
           (void) ti;							\
           (void) si;							\
           if (t == info->tmask) {					\
              tex10 -= info->tsize;					\
              tex11 -= info->tsize;					\
           }								\
           if (s == info->smask) {					\
              tex01 -= info->tbytesline;				\
              tex11 -= info->tbytesline;				\
           }								\
           DO_TEX;							\
           span->red += span->redStep;					\
	   span->green += span->greenStep;				\
           span->blue += span->blueStep;				\
	   span->alpha += span->alphaStep;				\
	   span->intTex[0] += span->intTexStep[0];			\
	   span->intTex[1] += span->intTexStep[1];			\
           dest += 4;							\
	}


   GLuint i;

   span->intTex[0] -= FIXED_HALF;
   span->intTex[1] -= FIXED_HALF;
   switch (info->filter) {
   case GL_NEAREST:
      switch (info->format) {
      case GL_RGB:
         switch (info->envmode) {
         case GL_MODULATE:
            SPAN_NEAREST(NEAREST_RGB;MODULATE,3);
            break;
         case GL_DECAL:
         case GL_REPLACE:
            SPAN_NEAREST(NEAREST_RGB_REPLACE,3);
            break;
         case GL_BLEND:
            SPAN_NEAREST(NEAREST_RGB;BLEND,3);
            break;
         case GL_ADD:
            SPAN_NEAREST(NEAREST_RGB;ADD,3);
            break;
         default:
            abort();
         }
         break;
      case GL_RGBA:
         switch(info->envmode) {
         case GL_MODULATE:
            SPAN_NEAREST(NEAREST_RGBA;MODULATE,4);
            break;
         case GL_DECAL:
            SPAN_NEAREST(NEAREST_RGBA;DECAL,4);
            break;
         case GL_BLEND:
            SPAN_NEAREST(NEAREST_RGBA;BLEND,4);
            break;
         case GL_ADD:
            SPAN_NEAREST(NEAREST_RGBA;ADD,4);
            break;
         case GL_REPLACE:
            SPAN_NEAREST(NEAREST_RGBA_REPLACE,4);
            break;
         default:
            abort();
         }
         break;
      }
      break;

   case GL_LINEAR:
      span->intTex[0] -= FIXED_HALF;
      span->intTex[1] -= FIXED_HALF;
      switch (info->format) {
      case GL_RGB:
         switch (info->envmode) {
         case GL_MODULATE:
            SPAN_LINEAR(LINEAR_RGB;MODULATE,3);
            break;
         case GL_DECAL:
         case GL_REPLACE:
            SPAN_LINEAR(LINEAR_RGB;REPLACE,3);
            break;
         case GL_BLEND:
            SPAN_LINEAR(LINEAR_RGB;BLEND,3);
            break;
         case GL_ADD:
            SPAN_LINEAR(LINEAR_RGB;ADD,3);
            break;
         default:
            abort();
         }
         break;
      case GL_RGBA:
         switch (info->envmode) {
         case GL_MODULATE:
            SPAN_LINEAR(LINEAR_RGBA;MODULATE,4);
            break;
         case GL_DECAL:
            SPAN_LINEAR(LINEAR_RGBA;DECAL,4);
            break;
         case GL_BLEND:
            SPAN_LINEAR(LINEAR_RGBA;BLEND,4);
            break;
         case GL_ADD:
            SPAN_LINEAR(LINEAR_RGBA;ADD,4);
            break;
         case GL_REPLACE:
            SPAN_LINEAR(LINEAR_RGBA;REPLACE,4);
            break;
         default:
            abort();
         }		    break;
      }
      break;
   }

#undef SPAN_NEAREST
#undef SPAN_LINEAR
}


static INLINE void
affine_span(GLcontext *ctx, struct sw_span *span,
            struct affine_info *info)
{
   if (info->zReject && span_hidden(ctx, span))
      return;

   affine_fill(span, info, span->array->rgba[0], span->end);

   span->interpMask &= ~SPAN_RGBA;
   ASSERT(span->arrayMask & SPAN_RGBA);
   _mesa_write_rgba_span(ctx, span);
}



/*
 * Render an RGB/RGBA textured triangle without perspective correction.
 */
static void affine_textured_triangle( GLcontext *ctx,
				      const SWvertex *v0,
				      const SWvertex *v1,
				      const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_ALPHA 1
#define INTERP_INT_TEX 1
#define S_SCALE twidth
#define T_SCALE theight

#define SETUP_CODE							\
   struct affine_info info;						\
   struct gl_texture_unit *unit = ctx->Texture.Unit+0;			\
   struct gl_texture_object *obj = unit->Current2D;			\
   const GLint b = obj->BaseLevel;					\
   const GLfloat twidth = (GLfloat) obj->Image[b]->Width;		\
   const GLfloat theight = (GLfloat) obj->Image[b]->Height;		\
   info.texture = (const GLchan *) obj->Image[b]->Data;			\
   info.twidth_log2 = obj->Image[b]->WidthLog2;				\
   info.smask = obj->Image[b]->Width - 1;				\
   info.tmask = obj->Image[b]->Height - 1;				\
   info.format = obj->Image[b]->Format;					\
   info.filter = obj->MinFilter;					\
   info.envmode = unit->EnvMode;					\
   info.zReject = span_zreject_ok(ctx);					\
   span.arrayMask |= SPAN_RGBA;						\
									\
   if (info.envmode == GL_BLEND) {					\
      /* potential off-by-one error here? (1.0f -> 2048 -> 0) */	\
      info.er = FloatToFixed(unit->EnvColor[RCOMP] * CHAN_MAXF);	\
      info.eg = FloatToFixed(unit->EnvColor[GCOMP] * CHAN_MAXF);	\
      info.eb = FloatToFixed(unit->EnvColor[BCOMP] * CHAN_MAXF);	\
      info.ea = FloatToFixed(unit->EnvColor[ACOMP] * CHAN_MAXF);	\
   }									\
   if (!info.texture) {							\
      /* this shouldn't happen */					\
      return;								\
   }									\
									\
   switch (info.format) {						\
   case GL_ALPHA:							\
   case GL_LUMINANCE:							\
   case GL_INTENSITY:							\
      info.tbytesline = obj->Image[b]->Width;				\
      break;								\
   case GL_LUMINANCE_ALPHA:						\
      info.tbytesline = obj->Image[b]->Width * 2;			\
      break;								\
   case GL_RGB:								\
      info.tbytesline = obj->Image[b]->Width * 3;			\
      break;								\
   case GL_RGBA:							\
      info.tbytesline = obj->Image[b]->Width * 4;			\
      break;								\
   default:								\
      _mesa_problem(NULL, "Bad texture format in affine_texture_triangle");\
      return;								\
   }									\
   info.tsize = obj->Image[b]->Height * info.tbytesline;

#define RENDER_SPAN( span )   affine_span(ctx, &span, &info);

#include "s_tritemp.h"

}



/*
 * NOVA: perspective-correct span, rewritten.
 *
 * The original did a double-precision divide and two float->int conversions
 * for every pixel.  This version divides once every PERSP_SUBDIV pixels and
 * runs the integer affine inner loop in between -- what Quake's own software
 * renderer does, and visually indistinguishable at this step size.
 *
 * It also handles GL_NEAREST_MIPMAP_NEAREST / GL_LINEAR_MIPMAP_NEAREST, with
 * the level picked once per sub-span, so mipmapped textures no longer drop
 * to the general float path.
 */
#define PERSP_SUBDIV       16
#define PERSP_SUBDIV_SHIFT 4

static INLINE void
fast_persp_span(GLcontext *ctx, struct sw_span *span,
		const struct affine_info *base)
{
   struct affine_info info = *base;   /* per-level copy for affine_fill() */
   const GLfloat width = (GLfloat) (base->smask + 1);
   const GLfloat height = (GLfloat) (base->tmask + 1);
   const GLint comps = (base->format == GL_RGBA) ? 4 : 3;
   /* homogeneous coords, scaled so that s/q is in base-level texels */
   const GLfloat hsStep = span->texStepX[0][0] * width;
   const GLfloat htStep = span->texStepX[0][1] * height;
   const GLfloat hqStep = span->texStepX[0][3];
   GLfloat hs = span->tex[0][0] * width;
   GLfloat ht = span->tex[0][1] * height;
   GLfloat hq = span->tex[0][3];
   GLfloat invQ, sTex, tTex, sBias, tBias;
   GLfixed sFix, tFix;
   GLchan *dest = span->array->rgba[0];
   GLuint left = span->end;
   GLint curLevel = 0;

   if (base->zReject && span_hidden(ctx, span))
      return;

   invQ = (hq == 0.0F) ? 1.0F : (1.0F / hq);
   sTex = hs * invQ;
   tTex = ht * invQ;
   /* The texture repeats: drop the whole repeats at the left end so the
    * fixed-point coordinates stay small wherever the polygon is.
    */
   sBias = (GLfloat) IFLOOR(sTex / width) * width;
   tBias = (GLfloat) IFLOOR(tTex / height) * height;
   sFix = (GLfixed) ((sTex - sBias) * FIXED_SCALE);
   tFix = (GLfixed) ((tTex - tBias) * FIXED_SCALE);

   while (left > 0) {
      /* With no change in q along the span (orthographic projection, or a
       * polygon facing the viewer) the mapping is already linear, so the
       * whole span can be done in one run.
       */
      const GLuint n = (left > PERSP_SUBDIV && hqStep != 0.0F)
                     ? PERSP_SUBDIV : left;
      GLfixed sEnd, tEnd, sStep, tStep;
      GLint level = 0;

      if (base->maxLevel > 0) {
         /* texel footprint of a pixel at the left end of the sub-span;
          * level k is rho in (2^(k-0.5), 2^(k+0.5)]
          */
         const GLfloat dudx = FABSF((hsStep - sTex * hqStep) * invQ);
         const GLfloat dvdx = FABSF((htStep - tTex * hqStep) * invQ);
         const GLfloat dudy = FABSF((span->texStepY[0][0] * width
                                     - sTex * span->texStepY[0][3]) * invQ);
         const GLfloat dvdy = FABSF((span->texStepY[0][1] * height
                                     - tTex * span->texStepY[0][3]) * invQ);
         const GLfloat maxU = MAX2(dudx, dudy);
         const GLfloat maxV = MAX2(dvdx, dvdy);
         const GLfloat rho = MAX2(maxU, maxV);
         GLfloat threshold = 1.41421356F;
         while (level < base->maxLevel && rho > threshold) {
            level++;
            threshold *= 2.0F;
         }
         if (level != curLevel) {
            const struct gl_texture_image *img =
               base->obj->Image[base->obj->BaseLevel + level];
            info.texture = (const GLchan *) img->Data;
            info.twidth_log2 = img->WidthLog2;
            info.smask = img->Width - 1;
            info.tmask = img->Height - 1;
            info.tbytesline = img->Width * comps;
            info.tsize = img->Height * info.tbytesline;
            curLevel = level;
         }
      }

      /* exact coordinates at the right end of the sub-span */
      hs += hsStep * (GLfloat) n;
      ht += htStep * (GLfloat) n;
      hq += hqStep * (GLfloat) n;
      invQ = (hq == 0.0F) ? 1.0F : (1.0F / hq);
      sTex = hs * invQ;
      tTex = ht * invQ;
      sEnd = (GLfixed) ((sTex - sBias) * FIXED_SCALE);
      tEnd = (GLfixed) ((tTex - tBias) * FIXED_SCALE);
      if (n == PERSP_SUBDIV) {
         sStep = (sEnd - sFix) >> PERSP_SUBDIV_SHIFT;
         tStep = (tEnd - tFix) >> PERSP_SUBDIV_SHIFT;
      }
      else {
         sStep = (sEnd - sFix) / (GLint) n;
         tStep = (tEnd - tFix) / (GLint) n;
      }

      /* affine_fill() expects the half-texel bias s_tritemp.h gives
       * INTERP_INT_TEX coordinates, and removes it again itself.
       */
      span->intTex[0] = (sFix >> level) + FIXED_HALF;
      span->intTex[1] = (tFix >> level) + FIXED_HALF;
      span->intTexStep[0] = sStep >> level;
      span->intTexStep[1] = tStep >> level;
      affine_fill(span, &info, dest, n);

      dest += 4 * n;
      sFix = sEnd;
      tFix = tEnd;
      left -= n;
   }

   ASSERT(span->arrayMask & SPAN_RGBA);
   _mesa_write_rgba_span(ctx, span);
}


/*
 * Render an perspective corrected RGB/RGBA textured triangle.
 * The Q (aka V in Mesa) coordinate must be zero such that the divide
 * by interpolated Q/W comes out right.
 *
 */
static void persp_textured_triangle_full( GLcontext *ctx,
				     const SWvertex *v0,
				     const SWvertex *v1,
				     const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_ALPHA 1
#define INTERP_TEX 1

#define SETUP_CODE							\
   struct affine_info info;						\
   const struct gl_texture_unit *unit = ctx->Texture.Unit+0;		\
   const struct gl_texture_object *obj = unit->Current2D;		\
   const GLint b = obj->BaseLevel;					\
   info.texture = (const GLchan *) obj->Image[b]->Data;			\
   info.twidth_log2 = obj->Image[b]->WidthLog2;				\
   info.smask = obj->Image[b]->Width - 1;				\
   info.tmask = obj->Image[b]->Height - 1;				\
   info.format = obj->Image[b]->Format;					\
   info.filter = obj->MagFilter;					\
   info.envmode = unit->EnvMode;					\
   info.zReject = span_zreject_ok(ctx);					\
   info.obj = obj;							\
   info.maxLevel = (obj->MinFilter == obj->MagFilter)			\
                 ? 0 : (obj->_MaxLevel - obj->BaseLevel);		\
									\
   if (info.envmode == GL_BLEND) {					\
      /* potential off-by-one error here? (1.0f -> 2048 -> 0) */	\
      info.er = FloatToFixed(unit->EnvColor[RCOMP] * CHAN_MAXF);	\
      info.eg = FloatToFixed(unit->EnvColor[GCOMP] * CHAN_MAXF);	\
      info.eb = FloatToFixed(unit->EnvColor[BCOMP] * CHAN_MAXF);	\
      info.ea = FloatToFixed(unit->EnvColor[ACOMP] * CHAN_MAXF);	\
   }									\
   if (!info.texture) {							\
      /* this shouldn't happen */					\
      return;								\
   }									\
									\
   switch (info.format) {						\
   case GL_ALPHA:							\
   case GL_LUMINANCE:							\
   case GL_INTENSITY:							\
      info.tbytesline = obj->Image[b]->Width;				\
      break;								\
   case GL_LUMINANCE_ALPHA:						\
      info.tbytesline = obj->Image[b]->Width * 2;			\
      break;								\
   case GL_RGB:								\
      info.tbytesline = obj->Image[b]->Width * 3;			\
      break;								\
   case GL_RGBA:							\
      info.tbytesline = obj->Image[b]->Width * 4;			\
      break;								\
   default:								\
      _mesa_problem(NULL, "Bad texture format in persp_textured_triangle");\
      return;								\
   }									\
   info.tsize = obj->Image[b]->Height * info.tbytesline;

#define RENDER_SPAN( span )			\
   span.interpMask &= ~SPAN_RGBA;		\
   span.arrayMask |= SPAN_RGBA;			\
   fast_persp_span(ctx, &span, &info);

#include "s_tritemp.h"

}


/*
 * NOVA: affine span for a triangle that may be mipmapped.  The level is the
 * same for the whole triangle and is worked out on its first span from the
 * texcoord gradients s_tritemp.h has already computed (in base-level texels
 * per pixel).  After that a span costs no floating point at all.
 */
static INLINE void
affine_mip_span(GLcontext *ctx, struct sw_span *span, struct affine_info *info,
                GLfloat dsdx, GLfloat dsdy, GLfloat dtdx, GLfloat dtdy)
{
   if (info->zReject && span_hidden(ctx, span))
      return;

   if (info->level < 0) {
      GLint level = 0;
      if (info->maxLevel > 0) {
         const GLfloat ax = FABSF(dsdx), ay = FABSF(dsdy);
         const GLfloat bx = FABSF(dtdx), by = FABSF(dtdy);
         const GLfloat maxU = MAX2(ax, ay);
         const GLfloat maxV = MAX2(bx, by);
         const GLfloat rho = MAX2(maxU, maxV);
         GLfloat threshold = 1.41421356F;
         while (level < info->maxLevel && rho > threshold) {
            level++;
            threshold *= 2.0F;
         }
         if (level > 0) {
            const struct gl_texture_image *img =
               info->obj->Image[info->obj->BaseLevel + level];
            const GLint comps = (info->format == GL_RGBA) ? 4 : 3;
            info->texture = (const GLchan *) img->Data;
            info->twidth_log2 = img->WidthLog2;
            info->smask = img->Width - 1;
            info->tmask = img->Height - 1;
            info->tbytesline = img->Width * comps;
            info->tsize = img->Height * info->tbytesline;
         }
      }
      info->level = level;
   }

   if (info->level > 0) {
      /* s_tritemp.h hands us base-level coordinates with a half-texel bias;
       * move them to the chosen level and put the bias back for affine_fill().
       */
      const GLint level = info->level;
      const GLfixed sStep = span->intTexStep[0], tStep = span->intTexStep[1];
      span->intTex[0] = ((span->intTex[0] - FIXED_HALF) >> level) + FIXED_HALF;
      span->intTex[1] = ((span->intTex[1] - FIXED_HALF) >> level) + FIXED_HALF;
      span->intTexStep[0] = sStep >> level;
      span->intTexStep[1] = tStep >> level;
      affine_fill(span, info, span->array->rgba[0], span->end);
      span->intTexStep[0] = sStep;
      span->intTexStep[1] = tStep;
   }
   else {
      affine_fill(span, info, span->array->rgba[0], span->end);
   }

   span->interpMask &= ~SPAN_RGBA;
   ASSERT(span->arrayMask & SPAN_RGBA);
   _mesa_write_rgba_span(ctx, span);
}


/*
 * As affine_textured_triangle(), but copes with *_MIPMAP_NEAREST.
 */
static void affine_mip_textured_triangle( GLcontext *ctx,
                                          const SWvertex *v0,
                                          const SWvertex *v1,
                                          const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_ALPHA 1
#define INTERP_INT_TEX 1
#define S_SCALE twidth
#define T_SCALE theight

#define SETUP_CODE							\
   struct affine_info info;						\
   struct gl_texture_unit *unit = ctx->Texture.Unit+0;			\
   struct gl_texture_object *obj = unit->Current2D;			\
   const GLint b = obj->BaseLevel;					\
   const GLfloat twidth = (GLfloat) obj->Image[b]->Width;		\
   const GLfloat theight = (GLfloat) obj->Image[b]->Height;		\
   info.texture = (const GLchan *) obj->Image[b]->Data;			\
   info.twidth_log2 = obj->Image[b]->WidthLog2;				\
   info.smask = obj->Image[b]->Width - 1;				\
   info.tmask = obj->Image[b]->Height - 1;				\
   info.format = obj->Image[b]->Format;					\
   info.filter = obj->MagFilter;					\
   info.envmode = unit->EnvMode;					\
   info.zReject = span_zreject_ok(ctx);					\
   info.obj = obj;							\
   info.maxLevel = (obj->MinFilter == obj->MagFilter)			\
                 ? 0 : (obj->_MaxLevel - obj->BaseLevel);		\
   info.level = -1;							\
   span.arrayMask |= SPAN_RGBA;						\
									\
   if (info.envmode == GL_BLEND) {					\
      info.er = FloatToFixed(unit->EnvColor[RCOMP] * CHAN_MAXF);	\
      info.eg = FloatToFixed(unit->EnvColor[GCOMP] * CHAN_MAXF);	\
      info.eb = FloatToFixed(unit->EnvColor[BCOMP] * CHAN_MAXF);	\
      info.ea = FloatToFixed(unit->EnvColor[ACOMP] * CHAN_MAXF);	\
   }									\
   if (!info.texture) {							\
      /* this shouldn't happen */					\
      return;								\
   }									\
   info.tbytesline = obj->Image[b]->Width				\
                   * ((info.format == GL_RGBA) ? 4 : 3);		\
   info.tsize = obj->Image[b]->Height * info.tbytesline;

#define RENDER_SPAN( span )						\
   affine_mip_span(ctx, &span, &info, dsdx, dsdy, dtdx, dtdy);

#include "s_tritemp.h"

}


/*
 * NOVA: perspective-correct textured triangle, front end.  Triangles that do
 * not need perspective correction (see triangle_is_near_affine) take the
 * affine path, whose spans use no floating point at all.
 */
static void persp_textured_triangle( GLcontext *ctx,
				     const SWvertex *v0,
				     const SWvertex *v1,
				     const SWvertex *v2 )
{
   if (v0->texcoord[0][3] == 1.0F
       && v1->texcoord[0][3] == 1.0F
       && v2->texcoord[0][3] == 1.0F
       && triangle_is_near_affine(v0, v1, v2)) {
      affine_mip_textured_triangle(ctx, v0, v1, v2);
   }
   else {
      persp_textured_triangle_full(ctx, v0, v1, v2);
   }
}


#endif /* CHAN_BITS != GL_FLOAT */

                


#if CHAN_BITS == 8

/*
 * NOVA: integer fast path for the classic "texture x lightmap" case.
 *
 * Stock swrast sends every multitextured triangle down
 * multitextured_triangle() -> _mesa_write_texture_span(), which for each
 * pixel of each unit does a float divide (plus four more and a log2 when
 * mipmapping), converts the texcoords back to integers, and fetches every
 * texel through img->FetchTexel.  A bilinear lightmap alone is four indirect
 * calls and four float->int conversions per pixel.  That is the path Quake
 * engines hit for the whole world, and on a 68k FPU it is brutal.
 *
 * This path handles exactly two enabled 2D units:
 *
 *   unit 0:  RGB or RGBA texture, GL_REPLACE
 *   unit 1:  RGB texture, GL_MODULATE or GL_COMBINE(MODULATE, PREVIOUS,
 *            TEXTURE) with an RGB scale of 1, 2 or 4
 *
 * both GL_REPEAT, no border, filtered GL_NEAREST, GL_LINEAR or the matching
 * *_MIPMAP_NEAREST mode.  Anything else still takes the general path.
 *
 * Texture coordinates are perspective-divided every DUALTEX_SUBDIV pixels and
 * interpolated linearly in fixed point in between (the same trick Quake's own
 * software renderer uses), so the per-pixel work is integer only.  The mipmap
 * level is likewise chosen once per sub-span rather than per pixel.
 *
 * Fog, alpha test, depth test, blending and masking are still done by
 * _mesa_write_rgba_span(), so those all keep working.
 */

#define DUALTEX_SUBDIV       16
#define DUALTEX_SUBDIV_SHIFT 4

struct dualtex_unit
{
   const struct gl_texture_object *obj;
   GLint comps;              /* bytes per texel: 3 (RGB) or 4 (RGBA) */
   GLint linear;             /* bilinear filtering, else nearest */
   GLint maxLevel;           /* highest usable mip level relative to BaseLevel;
                              * zero when not mipmapping */
   GLfloat width, height;    /* size of the base level, in texels */
};

struct dualtex_info
{
   struct dualtex_unit unit[2];
   GLint rgbShift;           /* unit 1 GL_RGB_SCALE as a shift: 0, 1 or 2 */
   GLboolean zReject;        /* can spans be pre-tested against the Z buffer? */
};


/*
 * Can the given texture object be sampled by dualtex_span()?
 */
static GLboolean
dualtex_texture_ok( const struct gl_texture_unit *unit, GLboolean allowAlpha )
{
   const struct gl_texture_object *obj = unit->_Current;
   const struct gl_texture_image *base;
   GLint format, level, lastLevel;

   if (!obj || !obj->Complete || obj != unit->Current2D)
      return GL_FALSE;
   if (obj->WrapS != GL_REPEAT || obj->WrapT != GL_REPEAT)
      return GL_FALSE;

   lastLevel = obj->BaseLevel;
   if (obj->MinFilter == obj->MagFilter) {
      if (obj->MinFilter != GL_NEAREST && obj->MinFilter != GL_LINEAR)
         return GL_FALSE;
   }
   else if ((obj->MinFilter == GL_NEAREST_MIPMAP_NEAREST
             && obj->MagFilter == GL_NEAREST) ||
            (obj->MinFilter == GL_LINEAR_MIPMAP_NEAREST
             && obj->MagFilter == GL_LINEAR)) {
      if (unit->LodBias != 0.0F
          || obj->MinLod != -1000.0F || obj->MaxLod != 1000.0F)
         return GL_FALSE;
      lastLevel = obj->_MaxLevel;
   }
   else {
      /* trilinear, or min/mag filters that disagree: general path */
      return GL_FALSE;
   }

   base = obj->Image[obj->BaseLevel];
   if (!base)
      return GL_FALSE;
   format = base->TexFormat->MesaFormat;
   if (format != MESA_FORMAT_RGB && !(allowAlpha && format == MESA_FORMAT_RGBA))
      return GL_FALSE;

   for (level = obj->BaseLevel; level <= lastLevel; level++) {
      const struct gl_texture_image *img = obj->Image[level];
      if (!img || !img->Data || img->Border != 0
          || img->Width != img->RowStride
          || img->TexFormat->MesaFormat != format)
         return GL_FALSE;
   }
   return GL_TRUE;
}


/*
 * Is the current state one that dualtex_triangle() can render?
 */
static GLboolean
dualtex_usable( const GLcontext *ctx )
{
   const struct gl_texture_unit *unit0 = &ctx->Texture.Unit[0];
   const struct gl_texture_unit *unit1 = &ctx->Texture.Unit[1];

   if (ctx->Texture._EnabledUnits != 0x3
       || unit0->_ReallyEnabled != TEXTURE_2D_BIT
       || unit1->_ReallyEnabled != TEXTURE_2D_BIT
       || ctx->Light.Model.ColorControl != GL_SINGLE_COLOR
       || ctx->Fog.ColorSumEnabled)
      return GL_FALSE;

   if (unit0->EnvMode != GL_REPLACE)
      return GL_FALSE;

   if (unit1->EnvMode == GL_COMBINE_EXT) {
      /* RGB = PREVIOUS * TEXTURE * scale */
      if (unit1->CombineModeRGB != GL_MODULATE
          || unit1->CombineOperandRGB[0] != GL_SRC_COLOR
          || unit1->CombineOperandRGB[1] != GL_SRC_COLOR
          || unit1->CombineScaleShiftRGB > 2)
         return GL_FALSE;
      if (!((unit1->CombineSourceRGB[0] == GL_PREVIOUS_EXT
             && unit1->CombineSourceRGB[1] == GL_TEXTURE) ||
            (unit1->CombineSourceRGB[0] == GL_TEXTURE
             && unit1->CombineSourceRGB[1] == GL_PREVIOUS_EXT)))
         return GL_FALSE;
      /* Alpha must come out as the incoming alpha.  Unit 1 is an RGB
       * texture (alpha = 1), so PREVIOUS * TEXTURE does that too.
       */
      if (unit1->CombineScaleShiftA != 0)
         return GL_FALSE;
      if (unit1->CombineModeA == GL_REPLACE) {
         if (unit1->CombineSourceA[0] != GL_PREVIOUS_EXT
             || unit1->CombineOperandA[0] != GL_SRC_ALPHA)
            return GL_FALSE;
      }
      else if (unit1->CombineModeA == GL_MODULATE) {
         if (unit1->CombineOperandA[0] != GL_SRC_ALPHA
             || unit1->CombineOperandA[1] != GL_SRC_ALPHA)
            return GL_FALSE;
         if (!((unit1->CombineSourceA[0] == GL_PREVIOUS_EXT
                && unit1->CombineSourceA[1] == GL_TEXTURE) ||
               (unit1->CombineSourceA[0] == GL_TEXTURE
                && unit1->CombineSourceA[1] == GL_PREVIOUS_EXT)))
            return GL_FALSE;
      }
      else {
         return GL_FALSE;
      }
   }
   else if (unit1->EnvMode != GL_MODULATE) {
      return GL_FALSE;
   }

   return dualtex_texture_ok(unit0, GL_TRUE)
       && dualtex_texture_ok(unit1, GL_FALSE);
}


/*
 * Per-triangle setup.  Returns GL_FALSE if there is nothing to draw with.
 */
static GLboolean
dualtex_setup( const GLcontext *ctx, struct dualtex_info *info )
{
   const struct gl_texture_unit *unit1 = &ctx->Texture.Unit[1];
   GLuint u;

   for (u = 0; u < 2; u++) {
      const struct gl_texture_object *obj = ctx->Texture.Unit[u]._Current;
      const struct gl_texture_image *img = obj->Image[obj->BaseLevel];
      struct dualtex_unit *du = &info->unit[u];
      if (!img->Data)
         return GL_FALSE;       /* this shouldn't happen */
      du->obj = obj;
      du->comps = (img->TexFormat->MesaFormat == MESA_FORMAT_RGBA) ? 4 : 3;
      du->linear = (obj->MagFilter == GL_LINEAR);
      du->maxLevel = (obj->MinFilter == obj->MagFilter)
                   ? 0 : (obj->_MaxLevel - obj->BaseLevel);
      du->width = (GLfloat) img->Width;
      du->height = (GLfloat) img->Height;
   }

   info->rgbShift = (unit1->EnvMode == GL_COMBINE_EXT)
                  ? (GLint) unit1->CombineScaleShiftRGB : 0;

   info->zReject = span_zreject_ok(ctx);
   return GL_TRUE;
}


/* a + (b - a) * f, with f in [0,255] */
#define DUALTEX_LERP(a, b, f) \
   ((GLint) (a) + ((((GLint) (b) - (GLint) (a)) * (f)) >> 8))

/* Point at the nearest texel of unit U. */
#define DUALTEX_NEAREST(U, TEX)						\
   const GLchan *TEX = data##U + comps##U *				\
      (((FixedToInt(t##U) & hmask##U) << wlog##U)			\
       + (FixedToInt(s##U) & wmask##U))

/* Bilinearly filter unit U into a local texel. */
#define DUALTEX_LINEAR(U, TEX)						\
   GLchan TEX[4];							\
   {									\
      const GLint col0 = FixedToInt(s##U) & wmask##U;			\
      const GLint col1 = (col0 + 1) & wmask##U;				\
      const GLint row0 = (FixedToInt(t##U) & hmask##U) << wlog##U;	\
      const GLint row1 = ((FixedToInt(t##U) + 1) & hmask##U) << wlog##U;\
      const GLint fs = (s##U & FIXED_FRAC_MASK) >> (FIXED_SHIFT - 8);	\
      const GLint ft = (t##U & FIXED_FRAC_MASK) >> (FIXED_SHIFT - 8);	\
      const GLchan *t00 = data##U + comps##U * (row0 + col0);		\
      const GLchan *t01 = data##U + comps##U * (row0 + col1);		\
      const GLchan *t10 = data##U + comps##U * (row1 + col0);		\
      const GLchan *t11 = data##U + comps##U * (row1 + col1);		\
      GLint top, bot;							\
      top = DUALTEX_LERP(t00[0], t01[0], fs);				\
      bot = DUALTEX_LERP(t10[0], t11[0], fs);				\
      TEX[0] = (GLchan) DUALTEX_LERP(top, bot, ft);			\
      top = DUALTEX_LERP(t00[1], t01[1], fs);				\
      bot = DUALTEX_LERP(t10[1], t11[1], fs);				\
      TEX[1] = (GLchan) DUALTEX_LERP(top, bot, ft);			\
      top = DUALTEX_LERP(t00[2], t01[2], fs);				\
      bot = DUALTEX_LERP(t10[2], t11[2], fs);				\
      TEX[2] = (GLchan) DUALTEX_LERP(top, bot, ft);			\
      if (comps##U == 4) {						\
         top = DUALTEX_LERP(t00[3], t01[3], fs);			\
         bot = DUALTEX_LERP(t10[3], t11[3], fs);			\
         TEX[3] = (GLchan) DUALTEX_LERP(top, bot, ft);			\
      }									\
   }

/* One sub-span: sample both units, modulate, store. */
#define DUALTEX_LOOP(FETCH0, FETCH1)					\
   for (i = 0; i < n; i++) {						\
      FETCH0(0, tex0);							\
      FETCH1(1, tex1);							\
      GLuint r = ((GLuint) tex0[0] * ((GLuint) tex1[0] + 1)) >> shift;	\
      GLuint g = ((GLuint) tex0[1] * ((GLuint) tex1[1] + 1)) >> shift;	\
      GLuint b = ((GLuint) tex0[2] * ((GLuint) tex1[2] + 1)) >> shift;	\
      if (shift != 8) {							\
         if (r > CHAN_MAX) r = CHAN_MAX;				\
         if (g > CHAN_MAX) g = CHAN_MAX;				\
         if (b > CHAN_MAX) b = CHAN_MAX;				\
      }									\
      dest[RCOMP] = (GLchan) r;						\
      dest[GCOMP] = (GLchan) g;						\
      dest[BCOMP] = (GLchan) b;						\
      dest[ACOMP] = (comps0 == 4) ? tex0[3]				\
                                  : (GLchan) FixedToInt(alpha);		\
      alpha += alphaStep;						\
      s0 += ds0;  t0 += dt0;						\
      s1 += ds1;  t1 += dt1;						\
      dest += 4;							\
   }


static void
dualtex_span( GLcontext *ctx, struct sw_span *span,
              const struct dualtex_info *info )
{
   /* homogeneous texcoords, pre-scaled so that s/q is in base-level texels */
   GLfloat hs[2], ht[2], hq[2];          /* at the start of the sub-span */
   GLfloat hsStep[2], htStep[2], hqStep[2];
   GLfloat sBias[2], tBias[2];           /* whole repeats removed, see below */
   GLfloat sTex[2], tTex[2], invQ[2];    /* divided values, start of sub-span */
   GLfixed sFix[2], tFix[2];
   GLchan *dest = span->array->rgba[0];
   const GLint shift = 8 - info->rgbShift;
   const GLfixed alphaStep = span->alphaStep;
   GLfixed alpha = span->alpha;
   GLuint left = span->end;
   GLuint u;

   if (info->zReject && span_hidden(ctx, span))
      return;

   for (u = 0; u < 2; u++) {
      const struct dualtex_unit *du = &info->unit[u];
      hs[u] = span->tex[u][0] * du->width;
      ht[u] = span->tex[u][1] * du->height;
      hq[u] = span->tex[u][3];
      hsStep[u] = span->texStepX[u][0] * du->width;
      htStep[u] = span->texStepX[u][1] * du->height;
      hqStep[u] = span->texStepX[u][3];

      invQ[u] = (hq[u] == 0.0F) ? 1.0F : (1.0F / hq[u]);
      sTex[u] = hs[u] * invQ[u];
      tTex[u] = ht[u] * invQ[u];

      /* The texture repeats, so drop the whole repeats at the left end of
       * the span.  That keeps the fixed-point coordinates small however far
       * from the texture origin the polygon is.
       */
      sBias[u] = (GLfloat) IFLOOR(sTex[u] / du->width) * du->width;
      tBias[u] = (GLfloat) IFLOOR(tTex[u] / du->height) * du->height;
      sFix[u] = (GLfixed) ((sTex[u] - sBias[u]) * FIXED_SCALE);
      tFix[u] = (GLfixed) ((tTex[u] - tBias[u]) * FIXED_SCALE);
   }

   while (left > 0) {
      const GLuint n = (left > DUALTEX_SUBDIV) ? DUALTEX_SUBDIV : left;
      const struct gl_texture_image *img[2];
      GLint level[2];
      GLfixed sStep[2], tStep[2];
      GLuint i;

      for (u = 0; u < 2; u++) {
         const struct dualtex_unit *du = &info->unit[u];
         GLfixed sEnd, tEnd;

         /* Mipmap level for this sub-span, from the texel footprint of a
          * pixel at its left end.  Same quantity compute_lambda() takes the
          * log2 of, but level k is simply rho in (2^(k-0.5), 2^(k+0.5)].
          */
         level[u] = 0;
         if (du->maxLevel > 0) {
            const GLfloat dudx = FABSF((hsStep[u] - sTex[u] * hqStep[u]) * invQ[u]);
            const GLfloat dvdx = FABSF((htStep[u] - tTex[u] * hqStep[u]) * invQ[u]);
            const GLfloat dudy = FABSF((span->texStepY[u][0] * du->width
                                        - sTex[u] * span->texStepY[u][3]) * invQ[u]);
            const GLfloat dvdy = FABSF((span->texStepY[u][1] * du->height
                                        - tTex[u] * span->texStepY[u][3]) * invQ[u]);
            const GLfloat maxU = MAX2(dudx, dudy);
            const GLfloat maxV = MAX2(dvdx, dvdy);
            const GLfloat rho = MAX2(maxU, maxV);
            GLfloat threshold = 1.41421356F;
            while (level[u] < du->maxLevel && rho > threshold) {
               level[u]++;
               threshold *= 2.0F;
            }
         }
         img[u] = du->obj->Image[du->obj->BaseLevel + level[u]];

         /* perspective-correct coordinates at the right end of the sub-span */
         hs[u] += hsStep[u] * (GLfloat) n;
         ht[u] += htStep[u] * (GLfloat) n;
         hq[u] += hqStep[u] * (GLfloat) n;
         invQ[u] = (hq[u] == 0.0F) ? 1.0F : (1.0F / hq[u]);
         sTex[u] = hs[u] * invQ[u];
         tTex[u] = ht[u] * invQ[u];
         sEnd = (GLfixed) ((sTex[u] - sBias[u]) * FIXED_SCALE);
         tEnd = (GLfixed) ((tTex[u] - tBias[u]) * FIXED_SCALE);

         if (n == DUALTEX_SUBDIV) {
            sStep[u] = (sEnd - sFix[u]) >> DUALTEX_SUBDIV_SHIFT;
            tStep[u] = (tEnd - tFix[u]) >> DUALTEX_SUBDIV_SHIFT;
         }
         else {
            sStep[u] = (sEnd - sFix[u]) / (GLint) n;
            tStep[u] = (tEnd - tFix[u]) / (GLint) n;
         }
      }

      {
         /* Everything the inner loop needs, in locals.  Coordinates are
          * moved to the chosen mip level, and bilinear sampling is centred
          * by the usual half-texel offset.
          */
         const GLchan *data0 = (const GLchan *) img[0]->Data;
         const GLchan *data1 = (const GLchan *) img[1]->Data;
         const GLint comps0 = info->unit[0].comps;
         const GLint comps1 = 3;
         const GLint wmask0 = img[0]->Width - 1,  hmask0 = img[0]->Height - 1;
         const GLint wmask1 = img[1]->Width - 1,  hmask1 = img[1]->Height - 1;
         const GLint wlog0 = img[0]->WidthLog2,   wlog1 = img[1]->WidthLog2;
         const GLint half0 = info->unit[0].linear ? FIXED_HALF : 0;
         const GLint half1 = info->unit[1].linear ? FIXED_HALF : 0;
         GLfixed s0 = (sFix[0] >> level[0]) - half0;
         GLfixed t0 = (tFix[0] >> level[0]) - half0;
         GLfixed s1 = (sFix[1] >> level[1]) - half1;
         GLfixed t1 = (tFix[1] >> level[1]) - half1;
         const GLfixed ds0 = sStep[0] >> level[0], dt0 = tStep[0] >> level[0];
         const GLfixed ds1 = sStep[1] >> level[1], dt1 = tStep[1] >> level[1];

         if (info->unit[0].linear) {
            if (info->unit[1].linear) {
               DUALTEX_LOOP(DUALTEX_LINEAR, DUALTEX_LINEAR)
            }
            else {
               DUALTEX_LOOP(DUALTEX_LINEAR, DUALTEX_NEAREST)
            }
         }
         else {
            if (info->unit[1].linear) {
               DUALTEX_LOOP(DUALTEX_NEAREST, DUALTEX_LINEAR)
            }
            else {
               DUALTEX_LOOP(DUALTEX_NEAREST, DUALTEX_NEAREST)
            }
         }
      }

      /* Start the next sub-span from the exact end point rather than the
       * accumulated steps, so rounding error cannot build up along the span.
       */
      for (u = 0; u < 2; u++) {
         sFix[u] = (GLfixed) ((sTex[u] - sBias[u]) * FIXED_SCALE);
         tFix[u] = (GLfixed) ((tTex[u] - tBias[u]) * FIXED_SCALE);
      }
      left -= n;
   }

   ASSERT(span->arrayMask & SPAN_RGBA);
   _mesa_write_rgba_span(ctx, span);
}

#undef DUALTEX_LERP
#undef DUALTEX_NEAREST
#undef DUALTEX_LINEAR
#undef DUALTEX_LOOP


/*
 * Render a perspective corrected, two-unit (texture * lightmap) triangle.
 */
static void dualtex_triangle( GLcontext *ctx,
                              const SWvertex *v0,
                              const SWvertex *v1,
                              const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_ALPHA 1
#define INTERP_MULTITEX 1

#define SETUP_CODE							\
   struct dualtex_info info;						\
   if (!dualtex_setup(ctx, &info))					\
      return;

#define RENDER_SPAN( span )			\
   span.interpMask &= ~SPAN_RGBA;		\
   span.arrayMask |= SPAN_RGBA;			\
   dualtex_span(ctx, &span, &info);

#include "s_tritemp.h"

}

#endif /* CHAN_BITS == 8 */


/*
 * Render a smooth-shaded, textured, RGBA triangle.
 * Interpolate S,T,R with perspective correction, w/out mipmapping.
 */
static void general_textured_triangle( GLcontext *ctx,
				       const SWvertex *v0,
				       const SWvertex *v1,
				       const SWvertex *v2 )
{
#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_SPEC 1
#define INTERP_ALPHA 1
#define INTERP_TEX 1

#define RENDER_SPAN( span )   _mesa_write_texture_span(ctx, &span);

#include "s_tritemp.h"
}



/*
 * This is the big one!
 * Interpolate Z, RGB, Alpha, specular, fog, and N sets of texture coordinates.
 * Yup, it's slow.
 */
static void
multitextured_triangle( GLcontext *ctx,
                        const SWvertex *v0,
                        const SWvertex *v1,
                        const SWvertex *v2 )
{

#define INTERP_Z 1
#define INTERP_FOG 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE
#define INTERP_RGB 1
#define INTERP_ALPHA 1
#define INTERP_SPEC 1
#define INTERP_MULTITEX 1

#define RENDER_SPAN( span )   _mesa_write_texture_span(ctx, &span);

#include "s_tritemp.h"

}


static void occlusion_zless_triangle( GLcontext *ctx,
				      const SWvertex *v0,
				      const SWvertex *v1,
				      const SWvertex *v2 )
{
   if (ctx->OcclusionResult) {
      return;
   }

#define DO_OCCLUSION_TEST
#define INTERP_Z 1
#define DEPTH_TYPE DEFAULT_SOFTWARE_DEPTH_TYPE

#define RENDER_SPAN( span )				\
   GLuint i;						\
   for (i = 0; i < span.end; i++) {			\
      GLdepth z = FixedToDepth(span.z);			\
      if (z < zRow[i]) {				\
         ctx->OcclusionResult = GL_TRUE;		\
         return;					\
      }							\
      span.z += span.zStep;				\
   }

#include "s_tritemp.h"
}

static void nodraw_triangle( GLcontext *ctx,
			     const SWvertex *v0,
			     const SWvertex *v1,
			     const SWvertex *v2 )
{
   (void) (ctx && v0 && v1 && v2);
}


/*
 * This is used when separate specular color is enabled, but not
 * texturing.  We add the specular color to the primary color,
 * draw the triangle, then restore the original primary color.
 * Inefficient, but seldom needed.
 */
void _swrast_add_spec_terms_triangle( GLcontext *ctx,
				      const SWvertex *v0,
				      const SWvertex *v1,
				      const SWvertex *v2 )
{
   SWvertex *ncv0 = (SWvertex *)v0; /* drop const qualifier */
   SWvertex *ncv1 = (SWvertex *)v1;
   SWvertex *ncv2 = (SWvertex *)v2;
#if CHAN_TYPE == GL_FLOAT
   GLfloat rSum, gSum, bSum;
#else
   GLint rSum, gSum, bSum;
#endif
   GLchan c[3][4];
   /* save original colors */
   COPY_CHAN4( c[0], ncv0->color );
   COPY_CHAN4( c[1], ncv1->color );
   COPY_CHAN4( c[2], ncv2->color );
   /* sum v0 */
   rSum = ncv0->color[0] + ncv0->specular[0];
   gSum = ncv0->color[1] + ncv0->specular[1];
   bSum = ncv0->color[2] + ncv0->specular[2];
   ncv0->color[0] = MIN2(rSum, CHAN_MAX);
   ncv0->color[1] = MIN2(gSum, CHAN_MAX);
   ncv0->color[2] = MIN2(bSum, CHAN_MAX);
   /* sum v1 */
   rSum = ncv1->color[0] + ncv1->specular[0];
   gSum = ncv1->color[1] + ncv1->specular[1];
   bSum = ncv1->color[2] + ncv1->specular[2];
   ncv1->color[0] = MIN2(rSum, CHAN_MAX);
   ncv1->color[1] = MIN2(gSum, CHAN_MAX);
   ncv1->color[2] = MIN2(bSum, CHAN_MAX);
   /* sum v2 */
   rSum = ncv2->color[0] + ncv2->specular[0];
   gSum = ncv2->color[1] + ncv2->specular[1];
   bSum = ncv2->color[2] + ncv2->specular[2];
   ncv2->color[0] = MIN2(rSum, CHAN_MAX);
   ncv2->color[1] = MIN2(gSum, CHAN_MAX);
   ncv2->color[2] = MIN2(bSum, CHAN_MAX);
   /* draw */
   SWRAST_CONTEXT(ctx)->SpecTriangle( ctx, ncv0, ncv1, ncv2 );
   /* restore original colors */
   COPY_CHAN4( ncv0->color, c[0] );
   COPY_CHAN4( ncv1->color, c[1] );
   COPY_CHAN4( ncv2->color, c[2] );
}



#ifdef DEBUG

/* record the current triangle function name */
const char *_mesa_triFuncName = NULL;

#define USE(triFunc)				\
do {						\
    _mesa_triFuncName = #triFunc;		\
    /*printf("%s\n", _mesa_triFuncName);*/	\
    swrast->Triangle = triFunc;			\
} while (0)

#else

#define USE(triFunc)  swrast->Triangle = triFunc;

#endif




/*
 * Determine which triangle rendering function to use given the current
 * rendering context.
 *
 * Please update the summary flag _SWRAST_NEW_TRIANGLE if you add or
 * remove tests to this code.
 */
void
_swrast_choose_triangle( GLcontext *ctx )
{
   SWcontext *swrast = SWRAST_CONTEXT(ctx);
   const GLboolean rgbmode = ctx->Visual.rgbMode;

   if (ctx->Polygon.CullFlag &&
       ctx->Polygon.CullFaceMode == GL_FRONT_AND_BACK) {
      USE(nodraw_triangle);
      return;
   }

   if (ctx->RenderMode==GL_RENDER) {

      if (ctx->Polygon.SmoothFlag) {
         _mesa_set_aa_triangle_function(ctx);
         ASSERT(swrast->Triangle);
         return;
      }

      if (ctx->Depth.OcclusionTest &&
          ctx->Depth.Test &&
          ctx->Depth.Mask == GL_FALSE &&
          ctx->Depth.Func == GL_LESS &&
          !ctx->Stencil.Enabled) {
         if ((rgbmode &&
              ctx->Color.ColorMask[0] == 0 &&
              ctx->Color.ColorMask[1] == 0 &&
              ctx->Color.ColorMask[2] == 0 &&
              ctx->Color.ColorMask[3] == 0)
             ||
             (!rgbmode && ctx->Color.IndexMask == 0)) {
            USE(occlusion_zless_triangle);
            return;
         }
      }

      if (ctx->Texture._EnabledUnits) {
         /* Ugh, we do a _lot_ of tests to pick the best textured tri func */
	 const struct gl_texture_object *texObj2D;
         const struct gl_texture_image *texImg;
         GLenum minFilter, magFilter, envMode;
         GLint format;
         texObj2D = ctx->Texture.Unit[0].Current2D;
         texImg = texObj2D ? texObj2D->Image[texObj2D->BaseLevel] : NULL;
         format = texImg ? texImg->TexFormat->MesaFormat : -1;
         minFilter = texObj2D ? texObj2D->MinFilter : (GLenum) 0;
         magFilter = texObj2D ? texObj2D->MagFilter : (GLenum) 0;
         envMode = ctx->Texture.Unit[0].EnvMode;

         /* First see if we can used an optimized 2-D texture function */
         if (ctx->Texture._EnabledUnits == 1
             && ctx->Texture.Unit[0]._ReallyEnabled == TEXTURE_2D_BIT
             && texObj2D->WrapS==GL_REPEAT
	     && texObj2D->WrapT==GL_REPEAT
             && texImg->Border==0
             && texImg->Width == texImg->RowStride
             && (format == MESA_FORMAT_RGB || format == MESA_FORMAT_RGBA)
	     && (minFilter == magFilter
#if CHAN_BITS == 8
                 /* NOVA: *_MIPMAP_NEAREST is handled by fast_persp_span() */
                 || dualtex_texture_ok(&ctx->Texture.Unit[0], GL_TRUE)
#endif
                 )
	     && ctx->Light.Model.ColorControl == GL_SINGLE_COLOR
	     && ctx->Texture.Unit[0].EnvMode != GL_COMBINE_EXT) {
	    if (ctx->Hint.PerspectiveCorrection==GL_FASTEST
                && minFilter == magFilter) {
	       if (minFilter == GL_NEAREST
		   && format == MESA_FORMAT_RGB
		   && (envMode == GL_REPLACE || envMode == GL_DECAL)
		   && ((swrast->_RasterMask == (DEPTH_BIT | TEXTURE_BIT)
			&& ctx->Depth.Func == GL_LESS
			&& ctx->Depth.Mask == GL_TRUE)
		       || swrast->_RasterMask == TEXTURE_BIT)
		   && ctx->Polygon.StippleFlag == GL_FALSE) {
		  if (swrast->_RasterMask == (DEPTH_BIT | TEXTURE_BIT)) {
		     USE(simple_z_textured_triangle);
		  }
		  else {
		     USE(simple_textured_triangle);
		  }
	       }
	       else {
#if (CHAN_BITS == 16 || CHAN_BITS == 32)
                  USE(general_textured_triangle);
#else
                  USE(affine_textured_triangle);
#endif
	       }
	    }
	    else {
#if (CHAN_BITS == 16 || CHAN_BITS == 32)
               USE(general_textured_triangle);
#else
               USE(persp_textured_triangle);
#endif
	    }
	 }
         else {
            /* general case textured triangles */
            if (ctx->Texture._EnabledUnits > 1) {
#if CHAN_BITS == 8
               /* NOVA: texture * lightmap in fixed point, if the state fits */
               if (dualtex_usable(ctx)) {
                  USE(dualtex_triangle);
               }
               else
#endif
               {
                  USE(multitextured_triangle);
               }
            }
            else {
               USE(general_textured_triangle);
            }
         }
      }
      else {
         ASSERT(!ctx->Texture._EnabledUnits);
	 if (ctx->Light.ShadeModel==GL_SMOOTH) {
	    /* smooth shaded, no texturing, stippled or some raster ops */
            if (rgbmode) {
	       USE(smooth_rgba_triangle);
            }
            else {
               USE(smooth_ci_triangle);
            }
	 }
	 else {
	    /* flat shaded, no texturing, stippled or some raster ops */
            if (rgbmode) {
	       USE(flat_rgba_triangle);
            }
            else {
               USE(flat_ci_triangle);
            }
	 }
      }
   }
   else if (ctx->RenderMode==GL_FEEDBACK) {
      USE(_mesa_feedback_triangle);
   }
   else {
      /* GL_SELECT mode */
      USE(_mesa_select_triangle);
   }
}
