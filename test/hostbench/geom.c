// Geometry-bound workloads for NovaMesa: lots of vertices and small primitives,
// as opposed to bench.c which is fill-rate bound. See build.sh.
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
extern const char *_mesa_triFuncName;
#define TRINAME (_mesa_triFuncName ? _mesa_triFuncName : "")
#else
#define TRINAME ""
#endif

static int W = 640, H = 480;
static AMesaContext *actx;
static GLuint tex, gear1, gear2, gear3;

static double now(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec + ts.tv_nsec*1e-9; }
static unsigned rnd_state = 12345;
static float frand(void){ rnd_state = rnd_state*1103515245u + 12345u; return ((rnd_state >> 8) & 0xffff) / 65535.0f; }

/* ---- the classic gear, as in example/Gears ------------------------------ */
static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth)
{
	GLint i; GLfloat r0, r1, r2, angle, da, u, v, len;
	r0 = inner_radius; r1 = outer_radius - tooth_depth / 2.0; r2 = outer_radius + tooth_depth / 2.0;
	da = 2.0 * M_PI / teeth / 4.0;
	glShadeModel(GL_FLAT);
	glNormal3f(0.0, 0.0, 1.0);
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
		glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
		if (i < teeth) {
			glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
			glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
		}
	}
	glEnd();
	glBegin(GL_QUADS);
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
	}
	glEnd();
	glNormal3f(0.0, 0.0, -1.0);
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
		glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
		if (i < teeth) {
			glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
			glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
		}
	}
	glEnd();
	glBegin(GL_QUADS);
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), -width * 0.5);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
		glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
	}
	glEnd();
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
		glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
		u = r2 * cos(angle + da) - r1 * cos(angle); v = r2 * sin(angle + da) - r1 * sin(angle);
		len = sqrt(u * u + v * v); u /= len; v /= len;
		glNormal3f(v, -u, 0.0);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
		glNormal3f(cos(angle), sin(angle), 0.0);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), -width * 0.5);
		u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da); v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
		glNormal3f(v, -u, 0.0);
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
		glNormal3f(cos(angle), sin(angle), 0.0);
	}
	glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
	glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);
	glEnd();
	glShadeModel(GL_SMOOTH);
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glNormal3f(-cos(angle), -sin(angle), 0.0);
		glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
		glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
	}
	glEnd();
}

/* ---- a tessellated sphere: many small triangles ------------------------- */
#define SLICES 20
#define STACKS 12
static float sph[STACKS+1][SLICES+1][3];
static void build_sphere(void)
{
	int i, j;
	for (j = 0; j <= STACKS; j++) for (i = 0; i <= SLICES; i++) {
		float a = i * 2.0f * (float)M_PI / SLICES, b = j * (float)M_PI / STACKS;
		sph[j][i][0] = sinf(b) * cosf(a); sph[j][i][1] = cosf(b); sph[j][i][2] = sinf(b) * sinf(a);
	}
}
/* mode: 0 = textured, colour per vertex (pre-lit, like a Quake alias model)
 *       1 = GL lit, normals, untextured
 *       2 = flat colour only */
static void draw_sphere(int mode)
{
	int i, j, k;
	for (j = 0; j < STACKS; j++) {
		glBegin(GL_TRIANGLE_STRIP);
		for (i = 0; i <= SLICES; i++) for (k = 0; k < 2; k++) {
			const float *v = sph[j + k][i];
			if (mode == 0) {
				float l = 0.35f + 0.65f * (v[1] * 0.5f + 0.5f);
				glColor3f(l, l, l);
				glTexCoord2f(i * (2.0f / SLICES), (j + k) * (1.0f / STACKS));
			}
			else if (mode == 1) glNormal3fv(v);
			glVertex3fv(v);
		}
		glEnd();
	}
}
static void spheres(int mode, float t)
{
	int n;
	for (n = 0; n < 24; n++) {
		glPushMatrix();
		glTranslatef(((n % 6) - 2.5f) * 2.6f, ((n / 6) - 1.5f) * 2.6f, -14.0f);
		glRotatef(t * 40.0f + n * 15.0f, 0.3f, 1.0f, 0.1f);
		draw_sphere(mode);
		glPopMatrix();
	}
}

static void set_3d(void)
{
	float h = (float) H / (float) W;
	glViewport(0, 0, W, H);
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-1.0, 1.0, -h, h, 2.0, 100.0);
	glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}
static void reset_state(void)
{
	glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
	glDisable(GL_NORMALIZE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glShadeModel(GL_SMOOTH);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glColor3f(1, 1, 1);
}

/* ---- workloads ---------------------------------------------------------- */
static void w_gears(float t)
{
	static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
	float angle = t * 70.0f;
	set_3d(); glTranslatef(0.0, 0.0, -18.0);
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glEnable(GL_CULL_FACE); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_NORMALIZE);
	glPushMatrix();
	glRotatef(20.0, 1.0, 0.0, 0.0); glRotatef(30.0, 0.0, 1.0, 0.0);
	glPushMatrix(); glTranslatef(-3.0, -2.0, 0.0); glRotatef(angle, 0.0, 0.0, 1.0); glCallList(gear1); glPopMatrix();
	glPushMatrix(); glTranslatef(3.1, -2.0, 0.0); glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0); glCallList(gear2); glPopMatrix();
	glPushMatrix(); glTranslatef(-3.1, 4.2, 0.0); glRotatef(-2.0 * angle - 25.0, 0.0, 0.0, 1.0); glCallList(gear3); glPopMatrix();
	glPopMatrix();
}
static void w_spheres_tex(float t)
{
	set_3d(); glEnable(GL_CULL_FACE); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex);
	spheres(0, t);
}
static void w_spheres_lit(float t)
{
	static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
	set_3d(); glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glEnable(GL_CULL_FACE); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
	spheres(1, t);
}
static void w_spheres_flat(float t)
{
	set_3d(); glEnable(GL_CULL_FACE); glShadeModel(GL_FLAT); glColor3f(0.2f, 0.7f, 0.9f);
	spheres(2, t);
}
static void set_2d(void)
{
	glViewport(0, 0, W, H);
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
	glMatrixMode(GL_MODELVIEW); glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
}
static void w_lines(float t)	/* additive glowing lines, arcade-shooter style */
{
	int i;
	set_2d(); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
	rnd_state = 777;
	glBegin(GL_LINES);
	for (i = 0; i < 4000; i++) {
		float x = frand() * W, y = frand() * H, a = frand() * 6.28f + t, l = 6.0f + frand() * 30.0f;
		glColor3f(frand(), frand(), frand());
		glVertex2f(x, y); glVertex2f(x + cosf(a) * l, y + sinf(a) * l);
	}
	glEnd();
}
static void w_quads2d(float t)	/* small alpha-blended sprites */
{
	int i;
	set_2d(); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex);
	rnd_state = 4242;
	glBegin(GL_QUADS);
	for (i = 0; i < 3000; i++) {
		float x = frand() * W, y = frand() * H + sinf(t + i) * 4.0f, s = 3.0f + frand() * 9.0f;
		glColor4f(frand(), frand(), frand(), 0.6f);
		glTexCoord2f(0, 0); glVertex2f(x, y);
		glTexCoord2f(1, 0); glVertex2f(x + s, y);
		glTexCoord2f(1, 1); glVertex2f(x + s, y + s);
		glTexCoord2f(0, 1); glVertex2f(x, y + s);
	}
	glEnd();
}

static struct { const char *name; void (*fn)(float); } work[] = {
	{ "gears (display lists, lit)",              w_gears },
	{ "spheres: textured, colour per vertex",    w_spheres_tex },
	{ "spheres: GL lit, smooth, untextured",     w_spheres_lit },
	{ "spheres: flat colour, untextured",        w_spheres_flat },
	{ "2D additive lines x4000",                 w_lines },
	{ "2D blended textured quads x3000",         w_quads2d },
};
#define NWORK (int)(sizeof(work)/sizeof(work[0]))

static unsigned checksum(void){ unsigned s = 0, i, *p = (unsigned*)actx->back_buffer; for (i = 0; i < (unsigned)(W*H); i++) s = s*31 + p[i]; return s; }
static void dump(const char *name){ FILE *f = fopen(name, "wb"); int i; fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (i = 0; i < W*H; i++) { unsigned px = ((unsigned*)actx->back_buffer)[i]; fputc((px>>16)&255,f); fputc((px>>8)&255,f); fputc(px&255,f); }
	fclose(f); }

int main(int argc, char **argv)
{
	static struct BitMap bm; static struct RastPort rp = { &bm }; static struct Screen scr; static struct Window win;
	static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 }, green[4] = { 0.0, 0.8, 0.2, 1.0 }, blue[4] = { 0.2, 0.2, 1.0, 1.0 };
	static unsigned char img[64*64*4];
	int w, f, x, y, lvl, sz, frames = 20; const char *dumpprefix = NULL;
	if (argc > 2) { W = atoi(argv[1]); H = atoi(argv[2]); }
	if (argc > 3) frames = atoi(argv[3]);
	if (argc > 4) dumpprefix = argv[4];
	win.Width = W; win.Height = H; win.RPort = &rp; win.WScreen = &scr;
	actx = amesa_create_context(&win);
	if (!actx) { printf("no ctx\n"); return 1; }
	amesa_make_current(actx);

	for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) { unsigned char *p = img + (y*64+x)*4; int c = ((x>>3) ^ (y>>3)) & 1;
		p[0] = c ? 230 : 110; p[1] = 140 + ((x*y)&63); p[2] = c ? 90 : 200; p[3] = 255; }
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	for (lvl = 0, sz = 64; sz >= 1; lvl++, sz >>= 1) glTexImage2D(GL_TEXTURE_2D, lvl, 3, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

	build_sphere();
	gear1 = glGenLists(1); glNewList(gear1, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red); gear(1.0, 4.0, 1.0, 20, 0.7); glEndList();
	gear2 = glGenLists(1); glNewList(gear2, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green); gear(0.5, 2.0, 2.0, 10, 0.7); glEndList();
	gear3 = glGenLists(1); glNewList(gear3, GL_COMPILE); glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue); gear(1.3, 2.0, 0.5, 10, 0.7); glEndList();

	printf("%dx%d, %d frames per workload\n", W, H, frames);
	printf("%-40s %9s  %s\n", "workload", "ms/frame", "last triangle func");
	for (w = 0; w < NWORK; w++) {
		double t0, t1; char nm[256];
		if (getenv("ONLY") && atoi(getenv("ONLY")) != w) continue;
		reset_state(); glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); work[w].fn(0); glFlush();
		t0 = now();
		for (f = 0; f < frames; f++) {
			reset_state();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			work[w].fn(f * 0.05f); glFlush();
			amesa_swap_buffers(actx);
		}
		t1 = now();
		printf("%-40s %9.2f  %s [%08x]\n", work[w].name, (t1-t0)*1000/frames, TRINAME, checksum());
		if (dumpprefix) { reset_state(); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); work[w].fn(0.3f); glFlush(); snprintf(nm, sizeof nm, "%s_w%d.ppm", dumpprefix, w); dump(nm); }
	}
	return swap_sink == 12345;
}
