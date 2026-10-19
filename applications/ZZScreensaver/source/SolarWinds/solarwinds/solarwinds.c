/*
 * Solar Winds simulation derived from solarWinds.cpp:
 * Copyright (C) 1999-2010 Terence M. Welsh.
 * Amiga/Core1 adaptation Copyright (C) 2026 Xanxi.
 *
 * GPL-2.0-or-later. See COPYING. No warranty.
 *
 * The six original presets, emission, particle integration and colours are
 * retained. Pointer forests become bounded arrays; uninitialised particles
 * are explicitly inactive. Display lists become batched quads. Screen-space
 * point/line quads use an RGB coverage texture because the RIX texture path
 * does not expose continuous texel alpha. Rasterisation/blending stays in RIX.
 */
#include "solarwinds.h"
#include "sw_gl.h"
#include <math.h>
#include <string.h>

const sw_preset sw_presets[6] = {
    {1, 30,2000,0,50, 20, 15, 10,40,"Regular"},
    {1, 50,3000,2,20, 10, 10, 10,10,"Cosmic Strings"},
    {1,300,3000,2, 5, 20,100, 15,70,"Cold Pricklies"},
    {2,400,1600,2,15, 20, 15, 10, 0,"Space Fur"},
    {1, 40,1200,1,20,100, 20,  4,50,"Jiggly"},
    {1,400,1200,0,40, 20,  1,100,50,"Undertow"}
};
typedef struct {
    float e[SW_MAX_EMITTERS][3];
    float p[SW_MAX_PARTICLES][6];
    int newer[SW_MAX_PARTICLES], older[SW_MAX_PARTICLES];
    int last[SW_MAX_EMITTERS], next;
    unsigned char active[SW_MAX_PARTICLES];
    float c[9], phase[9], speed[9];
} sw_wind;
static sw_wind winds[SW_MAX_WINDS];
static const sw_preset *cfg;
static uint32_t rng;
static unsigned char texture[64*64*3];
static int batch;
#define SW_TAU 6.28318530718f

static float random_float(float max)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) * (max / 16777216.0f);
}
static float clamp01(float f) { return f<0?0:f>1?1:f; }

/* Luminance is expanded to RGB with identical channels: ONE,ONE addition
 * then has exactly the intended colour modulation, without alpha-keying. */
static void make_texture(unsigned id, int kind)
{
    int x,y;
    for(y=0;y<64;y++) for(x=0;x<64;x++) {
        float u=(x-32)/32.0f,v=(y-32)/32.0f,r=sqrtf(u*u+v*v),t;
        if(kind==0) t=1-r;                  /* original 64x64 halo */
        else if(kind==1) t=(1-r)*16;        /* disk with a soft rim */
        else {                             /* transverse coverage for ribbons */
            float a=(x+.5f)/64.0f;
            t=fminf(a,1-a)*16;
        }
        unsigned char b=(unsigned char)(255*clamp01(t));
        unsigned at=(unsigned)(y*64+x)*3;
        texture[at]=texture[at+1]=texture[at+2]=b;
    }
    swBindTexture(SW_TEXTURE_2D,id);
    swTexParameter(SW_TEXTURE_2D,SW_TEXTURE_MAG_FILTER,SW_LINEAR);
    swTexParameter(SW_TEXTURE_2D,SW_TEXTURE_MIN_FILTER,SW_LINEAR);
    swTexImage2D(SW_TEXTURE_2D,0,3,64,64,0,SW_RGB,SW_UNSIGNED_BYTE,texture);
}

int sw_init(unsigned preset, uint32_t seed)
{
    int i,j;
    if(preset<1 || preset>6) return 0;
    cfg=&sw_presets[preset-1]; rng=seed?seed:0x58414e58u;
    memset(winds,0,sizeof winds);
    for(i=0;i<cfg->winds;i++) {
        sw_wind *w=&winds[i];
        for(j=0;j<cfg->emitters;j++) {
            w->e[j][0]=random_float(60)-30;
            w->e[j][1]=random_float(60)-30;
            w->e[j][2]=random_float(30)-15;
            w->last[j]=-1;
        }
        for(j=0;j<cfg->particles;j++) {
            w->p[j][2]=100;
            w->newer[j]=w->older[j]=-1;
        }
        for(j=0;j<9;j++) {
            w->phase[j]=random_float(SW_TAU);
            float s=(float)(cfg->wind_speed*cfg->wind_speed);
            w->speed[j]=random_float(.00005f*s)+.00001f*s;
        }
    }
    swViewport(0,0,SW_WIDTH,SW_HEIGHT);
    swDisable(SW_DEPTH_TEST); swDisable(SW_CULL_FACE); swDisable(SW_ALPHA_TEST);
    swEnable(SW_BLEND);
    swClearColor(0,0,0,1);
    swTexEnv(SW_TEXTURE_ENV,SW_TEXTURE_ENV_MODE,SW_MODULATE);
    make_texture(1,0); make_texture(2,1); make_texture(3,2);
    return 1;
}

void sw_step(void)
{
    int n,i,j;
    if(!cfg)return;
    const float ev=cfg->emitter_speed*.01f,pv=cfg->particle_speed*.01f;
    for(n=0;n<cfg->winds;n++) {
        sw_wind *w=&winds[n];
        for(i=0;i<9;i++) {
            w->phase[i]+=w->speed[i];
            if(w->phase[i]>SW_TAU)w->phase[i]-=SW_TAU;
            w->c[i]=cosf(w->phase[i]);
        }
        for(i=0;i<cfg->emitters;i++) {
            int p=w->next;
            w->e[i][2]+=ev;
            int reset=w->e[i][2]>15;
            if(reset) {
                w->e[i][0]=random_float(60)-30;
                w->e[i][1]=random_float(60)-30;
                w->e[i][2]=-15;
            }
            memcpy(w->p[p],w->e[i],3*sizeof(float));
            w->active[p]=1;
            if(cfg->geometry==2) {
                if(w->newer[p]>=0)w->older[w->newer[p]]=-1;
                w->newer[p]=-1;
                int last=w->last[i];
                if(last==p)last=-1;
                w->older[p]=reset?-1:last;
                if(!reset && last>=0)w->newer[last]=p;
                w->last[i]=p;
            }
            if(++w->next>=cfg->particles)w->next=0;
        }
        for(j=6;j<9;j++)w->c[j]*=9.0f/cfg->particle_speed;
        for(i=0;i<cfg->particles;i++) if(w->active[i]) {
            float *p=w->p[i],x=p[0],y=p[1],z=p[2];
            p[0]=x+(w->c[0]*y+w->c[1]*z)*pv;
            p[1]=y+(w->c[2]*z+w->c[3]*x)*pv;
            p[2]=z+(w->c[4]*x+w->c[5]*y)*pv;
            p[3]=clamp01(fabsf((p[0]-x)*w->c[6]));
            p[4]=clamp01(fabsf((p[1]-y)*w->c[7]));
            p[5]=clamp01(fabsf((p[2]-z)*w->c[8]));
        }
    }
}

static void vertex(float x,float y,float z,float s,float t,const float *rgb)
{
    swColor3f(rgb[0],rgb[1],rgb[2]); swTexCoord2f(s,t); swVertex3f(x,y,z);
}
static void start_quad(void) { if(!batch)swBegin(SW_QUADS); }
static void end_quad(void) { if(++batch==16){swEnd();batch=0;} }
static void end_batch(void) { if(batch){swEnd();batch=0;} }
static void sprite(float x,float y,float z,float half,const float *c)
{
    start_quad();
    vertex(x-half,y-half,z,0,0,c); vertex(x+half,y-half,z,1,0,c);
    vertex(x+half,y+half,z,1,1,c); vertex(x-half,y+half,z,0,1,c);
    end_quad();
}

/* Points and ribbons are expanded in screen coordinates on Core1. Only
 * their triangles/texels are rasterised, sampled and blended by the FPGA. */
static void ribbon(const float *p,const float *q,const float *pc,const float *qc,float width)
{
    float a[6],b[6];
    a[0]=p[0];a[1]=p[1];a[2]=15-p[2];memcpy(a+3,pc,3*sizeof(float));
    b[0]=q[0];b[1]=q[1];b[2]=15-q[2];memcpy(b+3,qc,3*sizeof(float));
    if(a[2]<1 && b[2]<1)return;
    if(a[2]<1 || b[2]<1) {
        float *lo=a[2]<1?a:b,*hi=a[2]<1?b:a;
        float f=(1-lo[2])/(hi[2]-lo[2]);
        for(int k=0;k<6;k++)lo[k]+=(hi[k]-lo[k])*f;
    }
    float ax=320+240*a[0]/a[2],ay=240+240*a[1]/a[2];
    float bx=320+240*b[0]/b[2],by=240+240*b[1]/b[2];
    float dx=bx-ax,dy=by-ay,d=sqrtf(dx*dx+dy*dy);
    if(!isfinite(d)||d<1e-6f)return;
    width=fmaxf(1,width)*.5f;
    float ox=-dy/d*width,oy=dx/d*width;
    start_quad();
    vertex(ax-ox,ay-oy,0,0,.5f,a+3);vertex(ax+ox,ay+oy,0,1,.5f,a+3);
    vertex(bx+ox,by+oy,0,1,.5f,b+3);vertex(bx-ox,by-oy,0,0,.5f,b+3);
    end_quad();
}

void sw_draw(float elapsed_seconds,int first_frame)
{
    if(!cfg)return;
    float frames=elapsed_seconds*60;
    if(!isfinite(frames)||frames<=0)frames=1;
    if(frames>8)frames=8;
    swMatrixMode(SW_PROJECTION);swLoadIdentity();
    swOrtho(0,640,0,480,-1,1);
    swMatrixMode(SW_MODELVIEW);swLoadIdentity();
    swDisable(SW_TEXTURE_2D);
    if(first_frame || !cfg->blur)swClear(SW_COLOR_BUFFER_BIT);
    else {
        /* The old image is copied by the presenter BEFORE this draw. For
         * low FPS the original per-frame fade is composed over elapsed ticks. */
        float alpha=1-powf(1-(.5f-cfg->blur*.0049f),frames);
        swBlendFunc(SW_SRC_ALPHA,SW_ONE_MINUS_SRC_ALPHA);
        swColor4f(0,0,0,alpha);
        swBegin(SW_QUADS);
        swVertex3f(0,0,0);swVertex3f(640,0,0);swVertex3f(640,480,0);swVertex3f(0,480,0);
        swEnd();
    }
    swBlendFunc(SW_ONE,SW_ONE);
    swEnable(SW_TEXTURE_2D);
    swBindTexture(SW_TEXTURE_2D,cfg->geometry==0?1:cfg->geometry==1?2:3);
    if(cfg->geometry==0) {
        swMatrixMode(SW_PROJECTION);swLoadIdentity();
        swFrustum(-4.0/3.0,4.0/3.0,-1,1,1,10000);
        swMatrixMode(SW_MODELVIEW);swLoadIdentity();swTranslatef(0,0,-15);
    }
    batch=0;
    const float black[3]={0,0,0};
    for(int n=0;n<cfg->winds;n++) {
        sw_wind *w=&winds[n];
        for(int i=0;i<cfg->particles;i++) if(w->active[i]) {
            const float *p=w->p[i];
            if(!isfinite(p[0])||!isfinite(p[1])||!isfinite(p[2]))continue;
            if(cfg->geometry==0) {
                if(15-p[2]>=1 && 15-p[2]<=10000)sprite(p[0],p[1],p[2],.02f*cfg->size,p+3);
            } else if(cfg->geometry==1) {
                float z=15-p[2];if(z<1 || z>10000)continue;
                float size=.04f*cfg->size*fmaxf(.01f,p[2]+40);
                sprite(320+240*p[0]/z,240+240*p[1]/z,0,fmaxf(1,size)*.5f,p+3);
            } else if(w->older[i]>=0 && w->active[w->older[i]]) {
                int k=w->older[i];
                ribbon(p,w->p[k],w->newer[i]<0?black:p+3,w->older[k]<0?black:w->p[k]+3,
                       .005f*cfg->size*fmaxf(.01f,p[2]+40));
            }
        }
    }
    end_batch();
}

int sw_needs_history(void) { return cfg && cfg->blur; }
const sw_preset *sw_current_preset(void) { return cfg; }
unsigned sw_particle_count(void) { return cfg?(unsigned)(cfg->particles*cfg->winds):0; }
unsigned sw_emitter_count(void) { return cfg?(unsigned)(cfg->emitters*cfg->winds):0; }
unsigned sw_active_particles(void)
{
    unsigned count=0;
    if(cfg)for(int n=0;n<cfg->winds;n++)for(int i=0;i<cfg->particles;i++)count+=winds[n].active[i]!=0;
    return count;
}
uint32_t sw_state_hash(void)
{
    uint32_t h=2166136261u,v;
    if(cfg)for(int n=0;n<cfg->winds;n++)for(int i=0;i<cfg->particles;i++)if(winds[n].active[i]) {
        for(int j=0;j<6;j++){memcpy(&v,&winds[n].p[i][j],4);h=(h^v)*16777619u;}
    }
    return h;
}
