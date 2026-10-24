/* ZZOpenLaraRIX A1: OpenLara TR1 loader + hardware room viewer.
 * No synthetic scene; no dependency on SDL, shaders, Linux or MorphOS.
 * Engine structures and parser: BeWorld2018/OpenLara 2c2bcd679553cac9f47573d2cf2cff82005fc2fd.
 */
#include "ol_api.h"
#include "ol_format.h"
extern "C" {
#include "zzgl.h"
int zz_rix_open(int,int,char*,int);
void zz_rix_begin_frame(void);
void zz_rix_end_frame(void);
void zz_rix_close(void);
void zz_rix_set_alpha_textures(int);
void zz_rix_stats(unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*);
}
static TR::Level *level;
static float cx,cy,cz,yaw,pitch;
static bool keys[256];
static int room;
static unsigned last_time, sample_time, sample_frames, frames, fps10, tris;
static u32 *textures;

static void camera_room(int n) {
    room=n % level->roomsCount;
    TR::Room &r=level->rooms[room];
    if(r.data.vCount<=0) ol_fail("E222 EMPTY ROOM",room,0);
    float minx=1e20f,maxx=-1e20f,minz=minx,maxz=maxx;
    for(int i=0;i<r.data.vCount;i++) {
        const short3 &p=r.data.vertices[i].pos;
        minx=min(minx,float(p.x)); maxx=max(maxx,float(p.x));
        minz=min(minz,float(p.z)); maxz=max(maxz,float(p.z));
    }
    cx=r.info.x+(minx+maxx)*0.5f; cz=r.info.z+(minz+maxz)*0.5f;
    cy=(float(r.info.yTop)+float(r.info.yBottom))*0.5f;
    yaw=0; pitch=0;
}
static void reset_camera() {
    camera_room(0);
    for(int i=0;i<level->entitiesBaseCount;i++) {
        TR::Entity &e=level->entities[i];
        if(e.type==TR::Entity::LARA && e.room>=0 && e.room<level->roomsCount) {
            room=e.room; cx=e.x; cy=e.y-512; cz=e.z;
            yaw=float(e.rotation)*3.14159265358979323846f/32768.0f; return;
        }
    }
}
static void validate_level() {
    if(level->version!=TR::VER_TR1_PC || level->roomsCount<=0 || level->roomsCount>4096)
        ol_fail("E220 LEVEL",level->version,level->roomsCount);
    if(!level->tiles8 || !level->palette || level->tilesCount<=0 || level->tilesCount>256)
        ol_fail("E221 TILES",level->tilesCount,0);
    for(int r=0;r<level->roomsCount;r++) {
        for(int j=0;j<level->rooms[r].portalsCount;j++) {
            int n=level->rooms[r].portals[j].roomIndex;
            if(n<0||n>=level->roomsCount) ol_fail("E227 PORTAL",r,n);
        }
        TR::Room::Data &d=level->rooms[r].data;
        if(d.vCount<0 || d.fCount<0) ol_fail("E222 COUNTS",r,d.fCount);
        for(int i=0;i<d.fCount;i++) {
            TR::Face &f=d.faces[i];
            if(f.colored || f.flags.texture>=level->objectTexturesCount)
                ol_fail("E223 FACE",r,i);
            TR::TextureInfo &t=level->objectTextures[f.flags.texture];
            if(t.tile>=level->tilesCount) ol_fail("E224 TILE",r,t.tile);
            for(int j=0;j<(f.triangle?3:4);j++) {
                if(f.vertices[j]>=d.vCount) ol_fail("E225 INDEX",r,f.vertices[j]);
                if(t.texCoord[j].x<0 || t.texCoord[j].x>255 || t.texCoord[j].y<0 || t.texCoord[j].y>255)
                    ol_fail("E226 UV",f.flags.texture,j);
            }
        }
    }
}
extern "C" void ol_init(const void *data,unsigned size,const char *name) {
    if(size<8 || size>64u*1024u*1024u || ((const unsigned char*)data)[0]!=0x20 ||
       ((const unsigned char*)data)[1] || ((const unsigned char*)data)[2] || ((const unsigned char*)data)[3])
        ol_fail("E200 TR1 PC HEADER",size,0);
    Stream stream(name,data,int(size));
    level=new TR::Level(stream);
    validate_level();
    printf("OL PARSE OK rooms=%d tiles=%d entities=%d\n",level->roomsCount,level->tilesCount,level->entitiesBaseCount);
#ifndef OL_VALIDATE_ONLY
    char why[96];
    if(!zz_rix_open(640,480,why,sizeof why)) ol_fail("E300 GPU OPEN",0,0);
    zzgl_set_deferred(0); /* validated direct transport for first hardware image */
    zz_rix_set_alpha_textures(1); /* G4C RGBA5551 supported by the supplied 0.9.1 pipeline */
    textures=new u32[level->tilesCount];
    zzglGenTextures(level->tilesCount,textures);
    unsigned char *rgba=new unsigned char[256*256*4];
    for(int t=0;t<level->tilesCount;t++) {
        for(int i=0;i<256*256;i++) {
            unsigned index=level->tiles8[t].index[i];
            Color24 c=level->palette[index];
            rgba[i*4]=c.r;rgba[i*4+1]=c.g;rgba[i*4+2]=c.b;rgba[i*4+3]=index?255:0;
        }
        zzglBindTexture(ZGL_TEXTURE_2D,textures[t]);
        zzglTexParameterf(ZGL_TEXTURE_2D,ZGL_TEXTURE_MIN_FILTER,ZGL_NEAREST);
        zzglTexParameterf(ZGL_TEXTURE_2D,ZGL_TEXTURE_MAG_FILTER,ZGL_NEAREST);
        zzglTexParameterf(ZGL_TEXTURE_2D,ZGL_TEXTURE_WRAP_S,ZGL_CLAMP);
        zzglTexParameterf(ZGL_TEXTURE_2D,ZGL_TEXTURE_WRAP_T,ZGL_CLAMP);
        zzglTexImage2D(ZGL_TEXTURE_2D,0,ZGL_RGBA,256,256,0,ZGL_RGBA,ZGL_UNSIGNED_BYTE,rgba);
        unsigned err=zzglGetError(); if(err) ol_fail("E301 TEXTURE",t,err);
    }
    delete[] rgba;
#endif
    reset_camera();
    printf("OL CAMERA room=%d pos=%d,%d,%d\n",room,int(cx),int(cy),int(cz));
    last_time=sample_time=ol_clock_us();
}
#ifndef OL_VALIDATE_ONLY
static void vertex(const TR::Room &r,const TR::Face &f,const TR::TextureInfo &t,int j) {
    const TR::Room::Data::Vertex &v=r.data.vertices[f.vertices[j]];
    zzglColor4ubv(&v.color.r);
    zzglTexCoord2f((float(t.texCoord[j].x)+0.5f)/256.0f,(float(t.texCoord[j].y)+0.5f)/256.0f);
    zzglVertex3f(float(r.info.x)+v.pos.x,float(v.pos.y),float(r.info.z)+v.pos.z);
}
static void draw_room(int index) {
    const TR::Room &r=level->rooms[index];
    // Group opaque/cutout faces by original 256x256 tile, with no repacking or resizing.
    for(int tile=0;tile<level->tilesCount;tile++) {
        bool active=false; unsigned nv=0;
        for(int i=0;i<r.data.fCount;i++) {
            const TR::Face &f=r.data.faces[i]; const TR::TextureInfo &t=level->objectTextures[f.flags.texture];
            if(t.tile!=tile) continue;
            if(!active) {zzglBindTexture(ZGL_TEXTURE_2D,textures[tile]);active=true;}
            // End every face: alpha state may change, <=6 vertices per Begin/End.
            if(t.attribute==1) zzglEnable(ZGL_ALPHA_TEST); else zzglDisable(ZGL_ALPHA_TEST);
            zzglBegin(ZGL_TRIANGLES);
            vertex(r,f,t,0);vertex(r,f,t,1);vertex(r,f,t,2);tris++;
            if(!f.triangle){vertex(r,f,t,0);vertex(r,f,t,2);vertex(r,f,t,3);tris++;}
            zzglEnd(); nv++;
        }
        (void)nv;
    }
}
// Small built-in diagnostic font, no game assets, drawn through the same hardware path.
static unsigned glyph(char c) {
    static const unsigned digits[10]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    if(c>='0'&&c<='9')return digits[c-'0'];
    switch(c){case 'F':return 0x79a4;case 'P':return 0x7be4;case 'S':return 0x79cf;case 'T':return 0x7492;case 'R':return 0x7bed;case 'I':return 0x7497;case 'O':return 0x7b6f;case 'M':return 0x5f6d;case '.':return 2;default:return 0;}
}
static void hud() {
    char text[80];sprintf(text,"FPS %u.%u  TRI %u  ROOM %d",fps10/10,fps10%10,tris,room);
    zzglDisable(ZGL_TEXTURE_2D);zzglDisable(ZGL_DEPTH_TEST);zzglDisable(ZGL_ALPHA_TEST);zzglDisable(ZGL_BLEND);
    zzglMatrixMode(ZGL_PROJECTION);zzglLoadIdentity();zzglOrtho(0,640,480,0,-1,1);
    zzglMatrixMode(ZGL_MODELVIEW);zzglLoadIdentity();zzglColor4f(1,1,0,1);
    for(unsigned k=0;text[k];k++) {unsigned g=glyph(text[k]);
        for(int y=0;y<5;y++)for(int x=0;x<3;x++)if(g&(1u<<(14-y*3-x))) {
            float a=8+k*8+x*2,b=8+y*2;
            zzglBegin(ZGL_QUADS);zzglVertex2f(a,b);zzglVertex2f(a+2,b);zzglVertex2f(a+2,b+2);zzglVertex2f(a,b+2);zzglEnd();
        }
    }
}
extern "C" void ol_tick() {
    unsigned now=ol_clock_us();float dt=min(float(now-last_time)*1e-6f,0.1f);last_time=now;
    int down,key;while(ol_key(&down,&key)) {if(key>=0&&key<256)keys[key]=down;
        if(down&&key==27){ol_stop();return;}
        if(down&&key=='r')reset_camera();
        if(down&&key=='n')camera_room(room+1);
        if(down&&key=='p')camera_room((room+level->roomsCount-1)%level->roomsCount);
    }
    int mx,my;ol_mouse(&mx,&my);yaw+=mx*0.003f;pitch=clamp(pitch+my*0.003f,-1.4f,1.4f);
    if(keys[130])yaw-=dt;if(keys[131])yaw+=dt;
    float f=(keys['w']||keys[128]?1:0)-(keys['s']||keys[129]?1:0);
    float side=(keys['d']?1:0)-(keys['a']?1:0);
    float speed=2048*dt;cx+=(sinf(yaw)*f+cosf(yaw)*side)*speed;
    cz+=(cosf(yaw)*f-sinf(yaw)*side)*speed;cy+=((keys['e']?1:0)-(keys['q']?1:0))*speed;
    float sy=sinf(yaw),co=cosf(yaw),sp=sinf(pitch),cp=cosf(pitch);
    float right[3]={co,0,-sy},up[3]={sy*sp,-cp,co*sp},back[3]={-sy*cp,-sp,-co*cp};
    float m[16]={right[0],up[0],back[0],0,right[1],up[1],back[1],0,right[2],up[2],back[2],0,
        -(right[0]*cx+right[1]*cy+right[2]*cz),-(up[0]*cx+up[1]*cy+up[2]*cz),-(back[0]*cx+back[1]*cy+back[2]*cz),1};
    zz_rix_begin_frame();
    zzglViewport(0,0,640,480);zzglScissor(0,0,640,480);
    zzglClearColor(0.03f,0.03f,0.05f,1);zzglClear(ZGL_COLOR_BUFFER_BIT|ZGL_DEPTH_BUFFER_BIT);
    zzglEnable(ZGL_TEXTURE_2D);zzglEnable(ZGL_DEPTH_TEST);zzglDepthFunc(ZGL_LEQUAL);zzglDepthMask(1);
    zzglDisable(ZGL_CULL_FACE);zzglDisable(ZGL_BLEND);zzglAlphaFunc(ZGL_GREATER,0.5f);
    zzglTexEnvf(ZGL_TEXTURE_ENV,ZGL_TEXTURE_ENV_MODE,ZGL_MODULATE);
    zzglMatrixMode(ZGL_PROJECTION);zzglLoadIdentity();zzglFrustum(-42.6667,42.6667,-32,32,64,65536);
    zzglMatrixMode(ZGL_MODELVIEW);zzglLoadMatrixf(m);
    tris=0;draw_room(room);
    // Adjacent rooms close the portal openings; no claim of full portal visibility yet.
    const TR::Room &r=level->rooms[room];
    for(int j=0;j<r.portalsCount;j++) {int n=r.portals[j].roomIndex;if(n<0||n>=level->roomsCount)ol_fail("E227 PORTAL",room,n);
        bool seen=n==room;for(int k=0;k<j;k++)if(r.portals[k].roomIndex==n)seen=true;
        if(!seen)draw_room(n);
    }
    unsigned err=zzglGetError();if(err)ol_fail("E310 DRAW",room,err);
    hud();zz_rix_end_frame(); // waits for the real GPU frame fence, then publishes P96
    frames++;now=ol_clock_us();
    if(now-sample_time>=1000000u){fps10=(unsigned)((double)(frames-sample_frames)*10000000.0/(now-sample_time));sample_frames=frames;sample_time=now;
        unsigned nf,gu,gm,pu,binds,freep;zz_rix_stats(&nf,&gu,&gm,&pu,&binds,&freep);
        printf("OL FRAME fps10=%u tris=%u room=%d fence_frames=%u gpu_wait_us=%u present_us=%u binds=%u\n",fps10,tris,room,nf,gu,pu,binds);
    }
}
#else
extern "C" void ol_tick() {}
#endif
extern "C" void ol_shutdown() {
#ifndef OL_VALIDATE_ONLY
    zz_rix_close();delete[] textures;textures=NULL;
#endif
    delete level;level=NULL;
}
