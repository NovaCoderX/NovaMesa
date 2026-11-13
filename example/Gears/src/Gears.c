#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <sys/time.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <clib/icon_protos.h>
#include <workbench/startup.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

#include <GL/amiga_mesa.h>
#include <GL/glu.h>

#define WIDTH 640
#define HEIGHT 480


// the startup message from workbench or 0 if started via cli
extern struct WBStartup *_WBenchMsg;

// Hardware screen - Only used when running in fullscreen.
static struct Screen *_customScreen = NULL;

// Hardware window - Used for both fullscreen and window modes.
static struct Window *_hardwareWindow = NULL;

struct Library *Picasso96Base = NULL;


static AMesaContext *context = NULL;

static UWORD _blankCursor[] = { 0x0000, 0x0000,   // reserved, must be NULL
		0x0000, 0x0000,   // 1 row of image data
		0x0000, 0x0000    // reserved, must be NULL
		};





// Demo code - START
static GLfloat view_rotx = 20.0, view_roty = 30.0, view_rotz = 0.0;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0;

static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth) {
	GLint i;
	GLfloat r0, r1, r2;
	GLfloat angle, da;
	GLfloat u, v, len;

	r0 = inner_radius;
	r1 = outer_radius - tooth_depth / 2.0;
	r2 = outer_radius + tooth_depth / 2.0;

	da = 2.0 * M_PI / teeth / 4.0;

	glShadeModel(GL_FLAT);

	glNormal3f(0.0, 0.0, 1.0);

	/* draw front face */
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

	/* draw front sides of teeth */
	glBegin(GL_QUADS);
	da = 2.0 * M_PI / teeth / 4.0;
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;

		glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
	}
	glEnd();

	glNormal3f(0.0, 0.0, -1.0);

	/* draw back face */
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

	/* draw back sides of teeth */
	glBegin(GL_QUADS);
	da = 2.0 * M_PI / teeth / 4.0;
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;

		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), -width * 0.5);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
		glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
	}
	glEnd();

	/* draw outward faces of teeth */
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i < teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;

		glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
		glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
		u = r2 * cos(angle + da) - r1 * cos(angle);
		v = r2 * sin(angle + da) - r1 * sin(angle);
		len = sqrt(u * u + v * v);
		u /= len;
		v /= len;
		glNormal3f(v, -u, 0.0);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
		glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
		glNormal3f(cos(angle), sin(angle), 0.0);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
		glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), -width * 0.5);
		u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
		v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
		glNormal3f(v, -u, 0.0);
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
		glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
		glNormal3f(cos(angle), sin(angle), 0.0);
	}

	glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
	glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);

	glEnd();

	glShadeModel(GL_SMOOTH);

	/* draw inside radius cylinder */
	glBegin(GL_QUAD_STRIP);
	for (i = 0; i <= teeth; i++) {
		angle = i * 2.0 * M_PI / teeth;
		glNormal3f(-cos(angle), -sin(angle), 0.0);
		glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
		glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
	}
	glEnd();
}

static void draw(void) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPushMatrix();
	glRotatef(view_rotx, 1.0, 0.0, 0.0);
	glRotatef(view_roty, 0.0, 1.0, 0.0);
	glRotatef(view_rotz, 0.0, 0.0, 1.0);

	glPushMatrix();
	glTranslatef(-3.0, -2.0, 0.0);
	glRotatef(angle, 0.0, 0.0, 1.0);
	glCallList(gear1);
	glPopMatrix();

	glPushMatrix();
	glTranslatef(3.1, -2.0, 0.0);
	glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0);
	glCallList(gear2);
	glPopMatrix();

	glPushMatrix();
	glTranslatef(-3.1, 4.2, 0.0);
	glRotatef(-2.0 * angle - 25.0, 0.0, 0.0, 1.0);
	glCallList(gear3);
	glPopMatrix();

	glPopMatrix();

	amesa_swap_buffers(context);
}

static void reshape(int width, int height) {
	GLfloat h = (GLfloat) height / (GLfloat) width;

	glViewport(0, 0, (GLint) width, (GLint) height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0, 0.0, -40.0);
}

static void init(void) {
	static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
	static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
	static GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
	static GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };

	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glEnable(GL_CULL_FACE);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_DEPTH_TEST);

	/* make the gears */
	gear1 = glGenLists(1);
	glNewList(gear1, GL_COMPILE);
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
	gear(1.0, 4.0, 1.0, 20, 0.7);
	glEndList();

	gear2 = glGenLists(1);
	glNewList(gear2, GL_COMPILE);
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
	gear(0.5, 2.0, 2.0, 10, 0.7);
	glEndList();

	gear3 = glGenLists(1);
	glNewList(gear3, GL_COMPILE);
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
	gear(1.3, 2.0, 0.5, 10, 0.7);
	glEndList();

	glEnable(GL_NORMALIZE);

	glClearColor(0, 0, 0, 1);

	reshape(WIDTH, HEIGHT);
}
// Demo code - FINISH


void exitT(void) {
	if (context) {
		amesa_destroy_context(context);
		context = NULL;
	}

	if (_hardwareWindow) {
		ClearPointer(_hardwareWindow);
		CloseWindow(_hardwareWindow);
		_hardwareWindow = NULL;
	}

	if (_customScreen) {
		// We opened the screen and therefore must close it (running in full screen mode).
		CloseScreen(_customScreen);
		_customScreen = NULL;
	}

	if (Picasso96Base) {
		CloseLibrary(Picasso96Base);
		Picasso96Base = NULL;
	}
}

static int isP96Mode(ULONG modeId) {
	if (modeId == INVALID_ID) {
		return 0;
	}

	return p96GetModeIDAttr(modeId, P96IDA_ISP96) ? 1 : 0;
}

static int isUsableP96Mode(ULONG modeId) {
	if (!isP96Mode(modeId)) {
		return 1;
	}

	if (ModeNotAvailable(modeId)) {
		return 0;
	}

	return 1;
}

static int isPixelFormat32(ULONG pixelFormat) {
	switch (pixelFormat)
	{
		case RGBFB_A8R8G8B8:
		case RGBFB_A8B8G8R8:
		case RGBFB_R8G8B8A8:
		case RGBFB_B8G8R8A8:
			return 1;

		default:
			return 0;
	}
}

static ULONG getP96Mode32(int width, int height) {
	ULONG modeId = INVALID_ID;
	ULONG nextid = NextDisplayInfo(INVALID_ID);

	while (nextid != INVALID_ID) {
		if (isUsableP96Mode(nextid)) {
			if (isPixelFormat32(p96GetModeIDAttr(nextid, P96IDA_RGBFORMAT))) {
				if (p96GetModeIDAttr(nextid, P96IDA_WIDTH) == (ULONG)width) {
					if (p96GetModeIDAttr(nextid, P96IDA_HEIGHT) == (ULONG)height) {
						// Success, exact match.
						modeId = nextid;
						break;
					}
				}
			}
		}

		nextid = NextDisplayInfo(nextid);
	}

	return modeId;
}

static struct Screen* CreateHardwareScreen(ULONG modeId, int width, int height, int bpp) {
	return OpenScreenTags(NULL, SA_Depth, bpp, SA_DisplayID, modeId, SA_Top, 0, SA_Left, 0, SA_Width, width, SA_Height, height, SA_Type,
			CUSTOMSCREEN, SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Draggable, FALSE, SA_Exclusive, TRUE, SA_AutoScroll, FALSE, TAG_DONE);
}

static struct Window* CreateHardwareWindowFullScreen(int width, int height, struct Screen *sreen) {
	return OpenWindowTags(NULL, WA_Left, 0, WA_Top, 0, WA_Width, width, WA_Height, height, WA_CustomScreen, (ULONG )sreen, WA_Backdrop,
			TRUE, WA_Borderless, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE, WA_ReportMouse, FALSE, WA_RMBTrap,
			TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW, TAG_END);
}

static struct Window* CreateHardwareWindow(int width, int height, struct Screen *screen) {

	LONG left = 0;
	LONG top = 0;

	left = (screen->Width - width) / 2;
	top = (screen->Height - height) / 2;

	return OpenWindowTags(NULL,
					WA_Left, left,
					WA_Top, top,
					WA_InnerWidth, width,
					WA_InnerHeight, height,
					WA_PubScreen, (ULONG)screen,
					WA_Backdrop, FALSE,
					WA_Borderless, FALSE,
					WA_DragBar, TRUE,
					WA_DepthGadget, TRUE,
					WA_CloseGadget, TRUE,
					WA_Activate, TRUE,
					WA_SimpleRefresh, TRUE,
					WA_NoCareRefresh, TRUE,
					WA_ReportMouse, FALSE,
					WA_RMBTrap, TRUE,
					WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW,
					TAG_END);

}

int main(int argc, char **argv) {
    struct timeval t1, t2;
    double elapsedtime;
    static GLint frames = 0;
    ULONG modeId = INVALID_ID;
    int fullscreen = 1;

    if (_WBenchMsg) {
    	struct DiskObject *diskObject;

		// Process Tooltypes.
		diskObject = GetDiskObject((char*)_WBenchMsg->sm_ArgList[0].wa_Name);
		if (diskObject != NULL) {
			// Process DOS command line flags.
			if (FindToolType(diskObject->do_ToolTypes, "windowed") != NULL) {
				fullscreen = 0;
			}
		}
    }

    // start timer
    gettimeofday(&t1, NULL);

	atexit(exitT);

	Picasso96Base = OpenLibrary("picasso96api.library", 2);

	modeId = getP96Mode32(WIDTH, HEIGHT);

	if (fullscreen) {
		_customScreen = CreateHardwareScreen(modeId, WIDTH, HEIGHT, p96GetModeIDAttr(modeId, P96IDA_BITSPERPIXEL));
		_hardwareWindow = CreateHardwareWindowFullScreen(WIDTH, HEIGHT, _customScreen);
	} else {
		struct Screen* defaultScreen;

		defaultScreen = LockPubScreen(NULL);
		_hardwareWindow = CreateHardwareWindow(WIDTH, HEIGHT, defaultScreen);
		UnlockPubScreen(NULL, defaultScreen);
	}

	SetPointer(_hardwareWindow, (UWORD* )_blankCursor, 1, 1, 0, 0);

	context = amesa_create_context(_hardwareWindow);
	amesa_make_current(context);

	init();

	while (frames < 240) {
		draw();
		angle += 2.0;
		frames++;
	}

    // stop timer
    gettimeofday(&t2, NULL);

    // compute and print the elapsed time in millisec
    elapsedtime = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
    elapsedtime += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms

    printf("GL_RENDERER = %s\n", (char *) glGetString(GL_RENDERER));
	printf("GL_VERSION = %s\n", (char *) glGetString(GL_VERSION));
	printf("GL_VENDOR = %s\n", (char *) glGetString(GL_VENDOR));
	//printf("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));

	// Test
	//printf("depth = %u\n", depth);

	GLfloat seconds = (elapsedtime / 1000.0);
	GLfloat fps = frames / seconds;
    printf("%d frames in %6.3f seconds = %6.3f FPS\n", frames, seconds, fps);

	return EXIT_SUCCESS;
}
