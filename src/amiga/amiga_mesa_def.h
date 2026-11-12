
#ifndef AMIGA_MESA_DEF_H
#define AMIGA_MESA_DEF_H

#include <GL/gl.h>
#include "context.h"

struct amigamesa_context {
	GLcontext *gl_ctx; /* The core GL/Mesa context */
	GLvisual *gl_visual; /* Describes the buffers */
	GLframebuffer *gl_buffer; /* Depth, stencil, accum, etc buffers */
	GLuint width, height; /* Drawable area */
	GLuint pitch; /* Bytes per row */
	GLuint clear_color; /* Color for clearing the pixel buffer */
	GLubyte *clear_buffer; /* Pixel buffer */
	GLubyte *back_buffer; /* Pixel buffer */
	GLuint **row; /* NOVA: back_buffer row pointers, y-flip already applied */
	struct Window *hardware_window; /* Intuition window */
	void (*swrast_line)(void); /* NOVA: swrast's own line function for the current state (a swrast_line_func) */
};

#endif
