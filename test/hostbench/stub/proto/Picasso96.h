#ifndef STUB_P96_H
#define STUB_P96_H
#include <stdlib.h>
#include <string.h>
typedef unsigned long ULONG_; 
#define ULONG unsigned int
#define APTR void*
#define WORD short
#define INVALID_ID (~0u)
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 2
#define P96IDA_ISP96 1
#define P96BMA_RGBFORMAT 1
enum { RGBFB_A8R8G8B8=6, RGBFB_A8B8G8R8, RGBFB_R8G8B8A8, RGBFB_B8G8R8A8 };
struct BitMap { int x; };
struct RastPort { struct BitMap *BitMap; };
struct ViewPort { int x; };
struct Screen { struct ViewPort ViewPort; };
struct Window { short Width, Height; signed char BorderLeft, BorderTop, BorderRight, BorderBottom; struct RastPort *RPort; struct Screen *WScreen; };
struct RenderInfo { APTR Memory; WORD BytesPerRow; WORD pad; int RGBFormat; };
static inline void *AllocVec(unsigned long n, int f){ return calloc(1,n); }
static inline void FreeVec(void *p){ free(p); }
static inline void CopyMemQuick(void *s, void *d, unsigned long n){ memcpy(d,s,n); }
static inline ULONG GetVPModeID(struct ViewPort *vp){ return 1; }
static inline int p96GetModeIDAttr(ULONG id, int a){ return 1; }
static inline ULONG p96GetBitMapAttr(struct BitMap *bm, int a){ return RGBFB_A8R8G8B8; }
extern unsigned swap_sink;
static inline void p96WritePixelArray(struct RenderInfo *ri, int sx,int sy, struct RastPort *rp,int dx,int dy,int w,int h){ swap_sink += ((unsigned*)ri->Memory)[0]; }
#endif
