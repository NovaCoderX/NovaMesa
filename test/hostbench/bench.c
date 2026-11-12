// Native harness: reproduces FitzQuake's world-pass GL state against NovaMesa swrast.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "libraries/Picasso96.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/amiga_mesa.h>
#include "amiga_mesa_def.h"

unsigned swap_sink;
#ifdef DEBUG
/* provided by the m_debug_*.c files, which build.sh leaves out */
void _math_test_all_transform_functions(char *d){}
void _math_test_all_normal_transform_functions(char *d){}
void _math_test_all_cliptest_functions(char *d){}
#endif
#ifdef DEBUG
extern const char *_mesa_triFuncName;
#define TRINAME _mesa_triFuncName
#else
#define TRINAME "?"
#endif

static int W = 640, H = 480;
static GLuint tex_world[4], tex_lm;
static AMesaContext *actx;

static double now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec + ts.tv_nsec*1e-9; }

static void make_textures(int minf, int magf)
{
	static unsigned char img[64*64*4], lm[128*128*3];
	int i, t, x, y, lvl, w;
	for (t = 0; t < 4; t++) {
		for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
			unsigned char *p = img + (y*64+x)*4;
			int c = ((x>>3) ^ (y>>3)) & 1;
			p[0] = c ? 200 : 90 + t*20; p[1] = 120 + ((x*y+t*37)&63); p[2] = c ? 60 : 160; p[3] = 255;
		}
		glBindTexture(GL_TEXTURE_2D, tex_world[t]);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		// mip chain (contents don't matter for timing; box filter would be nicer but irrelevant)
		for (lvl = 1, w = 32; w >= 1; lvl++, w >>= 1)
			glTexImage2D(GL_TEXTURE_2D, lvl, 3, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magf);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
	}
	for (i = 0; i < 128*128; i++) { lm[i*3] = 60 + (i*7)%120; lm[i*3+1] = 60 + (i*5)%120; lm[i*3+2] = 60 + (i*3)%120; }
	glBindTexture(GL_TEXTURE_2D, tex_lm);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, 128, 128, 0, GL_RGB, GL_UNSIGNED_BYTE, lm);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
}

// --- geometry: a corridor of <=128-unit quads, like BSP faces -------------
typedef struct { float v[4][7]; int tex; } bquad_t;   // x y z s t ls lt  (FitzQuake VERTEXSIZE layout)
static bquad_t quads[4096]; static int nquads;

static void addquad(float (*p)[3], int tex)
{
	bquad_t *q = &quads[nquads++]; int i;
	// pick the two dominant axes for texcoords
	float nx = fabsf((p[1][1]-p[0][1])*(p[2][2]-p[0][2]) - (p[1][2]-p[0][2])*(p[2][1]-p[0][1]));
	float ny = fabsf((p[1][2]-p[0][2])*(p[2][0]-p[0][0]) - (p[1][0]-p[0][0])*(p[2][2]-p[0][2]));
	float nz = fabsf((p[1][0]-p[0][0])*(p[2][1]-p[0][1]) - (p[1][1]-p[0][1])*(p[2][0]-p[0][0]));
	int a = 0, b = 1;
	if (nx >= ny && nx >= nz) { a = 1; b = 2; } else if (ny >= nx && ny >= nz) { a = 0; b = 2; }
	for (i = 0; i < 4; i++) {
		q->v[i][0] = p[i][0]; q->v[i][1] = p[i][1]; q->v[i][2] = p[i][2];
		q->v[i][3] = p[i][a] / 64.0f; q->v[i][4] = p[i][b] / 64.0f;
		q->v[i][5] = (p[i][a] + 4096.0f) / 2048.0f / 4.0f + 0.01f; q->v[i][6] = (p[i][b] + 4096.0f) / 2048.0f / 4.0f + 0.01f;
	}
	q->tex = tex;
}
static void build_scene(void)
{
	float z, x, y; float p[4][3];
#define P(i,X,Y,Z) p[i][0]=X; p[i][1]=Y; p[i][2]=Z
	for (z = -8; z > -1800; z -= 128) {
		for (x = -256; x < 256; x += 128) {
			P(0,x,-96,z); P(1,x+128,-96,z); P(2,x+128,-96,z-128); P(3,x,-96,z-128); addquad(p, 0);   // floor
			P(0,x, 96,z); P(1,x+128, 96,z); P(2,x+128, 96,z-128); P(3,x, 96,z-128); addquad(p, 1);   // ceiling
		}
		for (y = -96; y < 96; y += 96) {
			P(0,-256,y,z); P(1,-256,y+96,z); P(2,-256,y+96,z-128); P(3,-256,y,z-128); addquad(p, 2); // left
			P(0, 256,y,z); P(1, 256,y+96,z); P(2, 256,y+96,z-128); P(3, 256,y,z-128); addquad(p, 3); // right
		}
	}
	// cross walls with a doorway, far ones first in list => overdraw like texture-chain order
	for (z = -1800; z < -300; z += 500) {
		for (x = -256; x < 256; x += 128) for (y = -96; y < 96; y += 96) {
			if (z > -1700 && x >= -128 && x < 128 && y < 0) continue; // doorway
			P(0,x,y,z); P(1,x+128,y,z); P(2,x+128,y+96,z); P(3,x,y+96,z); addquad(p, (int)(-z/500)&3);
		}
	}
}

static void draw_mtex(void)
{
	int i, j, cur = -1;
	for (i = 0; i < nquads; i++) {
		bquad_t *q = &quads[i];
		if (q->tex != cur) {
			glActiveTextureARB(GL_TEXTURE0_ARB); glBindTexture(GL_TEXTURE_2D, tex_world[q->tex]);
			glActiveTextureARB(GL_TEXTURE1_ARB); cur = q->tex;
		}
		glBegin(GL_POLYGON);
		for (j = 0; j < 4; j++) {
			glMultiTexCoord2fARB(GL_TEXTURE0_ARB, q->v[j][3], q->v[j][4]);
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, q->v[j][5], q->v[j][6]);
			glVertex3fv(q->v[j]);
		}
		glEnd();
	}
}
static void draw_tex(int lm)
{
	int i, j, cur = -1;
	if (lm) glBindTexture(GL_TEXTURE_2D, tex_lm);
	for (i = 0; i < nquads; i++) {
		bquad_t *q = &quads[i];
		if (!lm && q->tex != cur) { glBindTexture(GL_TEXTURE_2D, tex_world[q->tex]); cur = q->tex; }
		glBegin(GL_POLYGON);
		for (j = 0; j < 4; j++) {
			if (lm) glTexCoord2f(q->v[j][5], q->v[j][6]); else glTexCoord2f(q->v[j][3], q->v[j][4]);
			glVertex3fv(q->v[j]);
		}
		glEnd();
	}
}

enum { M_MTEX_COMBINE, M_MTEX_MODULATE, M_2PASS_OVERBRIGHT, M_2PASS_MODULATE, M_TEXONLY };
static const char *mode_names[] = { "mtex+combine x2 (default)", "mtex+modulate (gl_overbright 0)",
	"2-pass blend DST*SRC*2 (-nomtex)", "2-pass blend ZERO,SRC (-nomtex, overbright 0)", "texture only (r_fullbright)" };

static const char *trinames[2];
static void frame(int mode, float yaw)
{
	float n = 4.0f, xm = n, ym = n * H / W;
	trinames[0] = trinames[1] = "";
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glViewport(0,0,W,H);
	glFrustum(-xm, xm, -ym, ym, n, 16384);
	glMatrixMode(GL_MODELVIEW); glLoadIdentity();
	glRotatef(yaw, 0, 1, 0); glRotatef(yaw*0.3f, 1, 0, 0);
	glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glEnable(GL_DEPTH_TEST);
	glClear(GL_DEPTH_BUFFER_BIT);

	switch (mode) {
	case M_MTEX_COMBINE:
		glActiveTextureARB(GL_TEXTURE1_ARB); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex_lm);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_PREVIOUS_EXT);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_TEXTURE);
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
		draw_mtex(); trinames[0] = TRINAME;
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glDisable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE0_ARB);
		break;
	case M_MTEX_MODULATE:
		glActiveTextureARB(GL_TEXTURE1_ARB); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex_lm);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		draw_mtex(); trinames[0] = TRINAME;
		glDisable(GL_TEXTURE_2D); glActiveTextureARB(GL_TEXTURE0_ARB);
		break;
	case M_2PASS_OVERBRIGHT:
	case M_2PASS_MODULATE:
		draw_tex(0); trinames[0] = TRINAME;
		glDepthMask(GL_FALSE); glEnable(GL_BLEND);
		if (mode == M_2PASS_OVERBRIGHT) glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); else glBlendFunc(GL_ZERO, GL_SRC_COLOR);
		draw_tex(1); trinames[1] = TRINAME;
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
		break;
	case M_TEXONLY:
		draw_tex(0); trinames[0] = TRINAME;
		break;
	}
	glFlush();
}

static unsigned checksum(void){ unsigned s = 0, i, *p = (unsigned*)actx->back_buffer; for (i = 0; i < (unsigned)(W*H); i++) s = s*31 + p[i]; return s; }
static void dump(const char *name){ FILE *f = fopen(name, "wb"); int i; unsigned char *p = actx->back_buffer; fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (i = 0; i < W*H; i++) {
		// back buffer holds native-endian 0xAARRGGBB words
		unsigned px = ((unsigned*)p)[i]; fputc((px>>16)&255,f); fputc((px>>8)&255,f); fputc(px&255,f); }
	fclose(f); }

int main(int argc, char **argv)
{
	static struct BitMap bm; static struct RastPort rp = { &bm }; static struct Screen scr; static struct Window win;
	int mode, f, frames = 20, hint, filt; const char *dumpprefix = NULL;
	if (argc > 2) { W = atoi(argv[1]); H = atoi(argv[2]); }
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) dumpprefix = argv[4];
	win.Width = W; win.Height = H; win.RPort = &rp; win.WScreen = &scr;
	actx = amesa_create_context(&win);
	if (!actx) { printf("no ctx\n"); return 1; }
	amesa_make_current(actx);

	// FitzQuake GL_SetupState
	glClearColor(0.15,0.15,0.15,0); glCullFace(GL_BACK); glFrontFace(GL_CW); glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.666);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glShadeModel(GL_FLAT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glDepthRange(0,1); glDepthFunc(GL_LEQUAL);
	glGenTextures(4, tex_world); glGenTextures(1, &tex_lm);
	build_scene();
	printf("%dx%d, %d quads, %d frames per config\n", W, H, nquads, frames);
	printf("%-12s %-26s %-46s %9s  %s\n", "hint", "world filter", "mode", "ms/frame", "triangle func(s)");

	for (hint = 0; hint < 2; hint++)
	for (filt = 0; filt < 3; filt++) {
		static const int minf[] = { GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST, GL_LINEAR };
		static const int magf[] = { GL_NEAREST, GL_NEAREST, GL_LINEAR };
		static const char *fn[] = { "NEAREST_MIPMAP_NEAREST(1)", "NEAREST(0)", "LINEAR(3)" };
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, hint ? GL_NICEST : GL_FASTEST);
		make_textures(minf[filt], magf[filt]);
		for (mode = 0; mode <= M_TEXONLY; mode++) {
			double t0, t1; char nm[256];
			if (getenv("ONLY")) { int a,b,c; sscanf(getenv("ONLY"), "%d,%d,%d", &a,&b,&c); if (a!=hint||b!=filt||c!=mode) continue; }
			glClearColor(0.15,0.15,0.15,0); glClear(GL_COLOR_BUFFER_BIT);
			frame(mode, 0);
			t0 = now();
			for (f = 0; f < frames; f++) { frame(mode, -20.0f + 40.0f * f / frames); amesa_swap_buffers(actx); }
			t1 = now();
			printf("%-12s %-26s %-46s %9.2f  %s %s  [%08x]\n", hint ? "NICEST" : "FASTEST", fn[filt], mode_names[mode], (t1-t0)*1000/frames, trinames[0], trinames[1], checksum());
			if (dumpprefix) { glClear(GL_COLOR_BUFFER_BIT); frame(mode, 12.0f); snprintf(nm, sizeof nm, "%s_h%d_f%d_m%d.ppm", dumpprefix, hint, filt, mode); dump(nm); }
		}
	}
	return swap_sink == 12345;
}
