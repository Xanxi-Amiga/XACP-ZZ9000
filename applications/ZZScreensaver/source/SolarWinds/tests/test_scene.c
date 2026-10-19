/* GPL-2.0-or-later. Compare the simulation against original upstream code. */
#include "solarwinds.h"
#include "history.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
void swref_init(unsigned,uint32_t);
void swref_step(void);
uint32_t swref_hash(void);
void swref_cleanup(void);
/* Scene GL calls are irrelevant to the simulation comparison. */
#define STUB(n,args) void n args {}
STUB(glBegin,(unsigned x)) STUB(glEnd,(void)) STUB(glBindTexture,(unsigned a,unsigned b))
STUB(glBlendFunc,(unsigned a,unsigned b)) STUB(glClear,(unsigned a))
STUB(glClearColor,(float a,float b,float c,float d)) STUB(glColor3f,(float a,float b,float c))
STUB(glColor4f,(float a,float b,float c,float d)) STUB(glDisable,(unsigned a)) STUB(glEnable,(unsigned a))
STUB(glFrustum,(double a,double b,double c,double d,double e,double f))
STUB(glLoadIdentity,(void)) STUB(glMatrixMode,(unsigned a))
STUB(glOrtho,(double a,double b,double c,double d,double e,double f))
STUB(glTexCoord2f,(float a,float b)) STUB(glTexEnvi,(unsigned a,unsigned b,int c))
STUB(glTexImage2D,(unsigned a,int b,int c,int d,int e,int f,unsigned g,unsigned h,const void*i))
STUB(glTexParameteri,(unsigned a,unsigned b,int c)) STUB(glTranslatef,(float a,float b,float c))
STUB(glVertex3f,(float a,float b,float c)) STUB(glViewport,(int a,int b,int c,int d))

int main(void)
{
    for(unsigned p=1;p<=6;p++) {
        assert(sw_init(p,0x58414e58));swref_init(p,0x58414e58);
        for(unsigned t=0;t<1000;t++) {
            sw_step();swref_step();
            if(sw_active_particles()==sw_particle_count()) {
                if(sw_state_hash()!=swref_hash()) {
                    fprintf(stderr,"Original simulation mismatch: preset %u step %u %08x != %08x\n",p,t+1,sw_state_hash(),swref_hash());
                    return 1;
                }
            }
        }
        assert(sw_active_particles()==sw_particle_count());swref_cleanup();
        printf("Original particle positions/colours match: preset %u, 1000 simulation steps\n",p);
    }
    unsigned char src[614400+1280],dst[sizeof src];
    for(unsigned i=0;i<sizeof src;i++)src[i]=(unsigned char)(i*73+7);
    memset(dst,0x5a,sizeof dst);
    sw_history_copy(dst,src,614400);
    for(unsigned i=0;i<614400;i+=2)assert(dst[i]==src[i+1] && dst[i+1]==src[i]);
    for(unsigned i=614400;i<sizeof dst;i++)assert(dst[i]==0x5a);
    puts("History copy: RGB565 byte order restored, fence row unchanged");
    return 0;
}
