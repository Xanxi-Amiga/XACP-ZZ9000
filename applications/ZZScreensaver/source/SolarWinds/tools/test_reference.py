#!/usr/bin/env python3
"""Compile the actual upstream wind class as a numerical reference."""
import os,pathlib,subprocess
P=pathlib.Path(__file__).resolve().parents[1];B=P/'tests/build';B.mkdir(parents=True,exist_ok=True)
source=(P/'upstream/solarwinds/solarWinds.cpp').read_text()
body=source[source.index('class wind{'):source.index('\n\nvoid draw(){')]
# Only initialise otherwise indeterminate heap data in the upstream harness.
# The comparisons begin after every particle slot has been emitted at least once.
body=body.replace('new float[6];','new float[6]();')
prefix=r'''
#include <cmath>
#include <cstdint>
#include <cstring>
#include "solarwinds.h"
class wind;
struct State {int dWinds,dEmitters,dParticles,dGeometry,dSize,dWindspeed,dEmitterspeed,dParticlespeed;};
static State ref_state;
static State& state(){return ref_state;}
static uint32_t rng;
static float rsRandf(float max){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)*(max/16777216.0f);}
#define NUMCONSTS 9
#define PIx2 6.28318530718f
#define glColor3fv(...) ((void)0)
#define glColor3f(...) ((void)0)
#define glPushMatrix(...) ((void)0)
#define glPopMatrix(...) ((void)0)
#define glTranslatef(...) ((void)0)
#define glCallList(...) ((void)0)
#define glPointSize(...) ((void)0)
#define glLineWidth(...) ((void)0)
#define glBegin(...) ((void)0)
#define glEnd(...) ((void)0)
#define glVertex3fv(...) ((void)0)
'''
# The original function caches per-preset speeds in statics; see the explicit
# adjustment below, which lets this harness run six presets in one process.
suffix=r'''
static wind *all;
extern "C" void swref_init(unsigned preset,uint32_t seed){
 const sw_preset& p=sw_presets[preset-1];
 ref_state={p.winds,p.emitters,p.particles,p.geometry,p.size,p.wind_speed,p.emitter_speed,p.particle_speed};
 rng=seed;all=new wind[p.winds];
}
extern "C" void swref_step(){for(int n=0;n<ref_state.dWinds;n++)all[n].update();}
extern "C" uint32_t swref_hash(){
 uint32_t h=2166136261u,v;
 for(int n=0;n<ref_state.dWinds;n++)for(int i=0;i<ref_state.dParticles;i++)for(int k=0;k<6;k++){
  memcpy(&v,&all[n].particles[i][k],4);h=(h^v)*16777619u;
 }return h;
}
extern "C" void swref_cleanup(){delete[] all;}
'''
# Use ordinary automatic cached values in the extracted reference so the six
# independent presets may be tested in one process. No formula is changed.
body=body.replace('static float evel','float evel').replace('static float pvel','float pvel')
body=body.replace('static float pointsize','float pointsize').replace('static float linesize','float linesize')
(B/'upstream_wind_reference.cpp').write_text(prefix+body+suffix)
f=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(P/'solarwinds')]
objects=[]
for compiler,src,extra in [('gcc',P/'solarwinds/solarwinds.c',['-DSW_MESA_PREVIEW']),
                         ('gcc',P/'solarwinds/history.c',[]),('gcc',P/'tests/test_scene.c',[]),
                         ('g++',B/'upstream_wind_reference.cpp',[])]:
 obj=B/(src.stem+'_ref.o');subprocess.run([compiler,*f,*extra,'-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
exe=B/'test_reference';subprocess.run(['g++',*f,*objects,'-lm','-o',str(exe)],check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')  # ptrace environment does not support LSan
subprocess.run([str(exe)],env=env,check=True)
