#!/usr/bin/env python3
"""Compile/test the actual preference and ASL selection section using host AmigaDOS stubs."""
from pathlib import Path
import subprocess, tempfile, textwrap, os
src=Path(__file__).parent/'amiga'/'ZZVGMPlay.c'
s=src.read_text()
core=s[s.index('/*\n * VGM directory preference,'):s.index('static void format_time(')]
header=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#define PATH_BUF_SIZE 512
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define TRUE 1
#define TAG_IGNORE 0
#define TAG_END 0
#define ASL_FileRequest 5
#define ASLFR_TitleText 111
#define ASLFR_DrawersOnly 112
#define ASLFR_InitialDrawer 113
#define ACCESS_READ 1
#define MODE_OLDFILE 100
#define MODE_NEWFILE 101
#define ULONG uintptr_t
#define LONG long
#define STRPTR char*
#define APTR void*
typedef FILE *BPTR;
struct TagItem {uintptr_t ti_Tag,ti_Data;};
struct FileRequester {char *fr_Drawer;};
static char g_last_drawer[PATH_BUF_SIZE], g_default_drawer[PATH_BUF_SIZE], status[100];
static char storage[PATH_MAX];
static char selected[PATH_BUF_SIZE];
static int accept = 1;
static void status_set(const char *s) {snprintf(status,sizeof(status),"%s",s);}
static const char * mapname(const char *p){
    static char name[PATH_MAX];
    if (!strncmp(p,"ENV:",4)) snprintf(name,sizeof(name),"%s/env-%s",storage,p+4);
    else if (!strncmp(p,"ENVARC:",7)) snprintf(name,sizeof(name),"%s/envarc-%s",storage,p+7);
    else snprintf(name,sizeof(name),"%s/%s",storage,p);
    return name;
}
static BPTR Lock(char *p,int mode) {
    (void)mode;
    if (!strcmp(p,"VGM:Arcade")||!strcmp(p,"Music:MD")||!strcmp(p,"Music:PSG")) return (BPTR)1;
    if (!strncmp(p,"ENV:",4)||!strncmp(p,"ENVARC:",7)){
        FILE *f=fopen(mapname(p),"rb");
        if(f) {fclose(f);return (BPTR)1;} return NULL;
    }
    return NULL;
}
static void UnLock(BPTR lock) {(void)lock;}
static BPTR Open(char *p,int mode) { return fopen(mapname(p),mode==MODE_OLDFILE?"rb":"wb"); }
static LONG Read(BPTR f,void *b,LONG n) {return (LONG)fread(b,1,(size_t)n,f);}
static LONG Write(BPTR f,void *b,LONG n) {return (LONG)fwrite(b,1,(size_t)n,f);}
static void Close(BPTR f) {fclose(f);}
static int DeleteFile(char *p) {return remove(mapname(p))==0;}
static struct FileRequester fr;
static struct FileRequester *AllocAslRequest(int typ,struct TagItem *t) {
    (void)typ;
    assert(t[0].ti_Tag==ASLFR_TitleText);
    assert(t[1].ti_Tag==ASLFR_DrawersOnly);
    assert(t[2].ti_Tag==TAG_IGNORE||t[2].ti_Tag==ASLFR_InitialDrawer);
    fr.fr_Drawer=selected;
    return &fr;
}
static int AslRequest(struct FileRequester *f, void *p) {(void)f;(void)p;return accept;}
static void FreeAslRequest(struct FileRequester *f){(void)f;}
'''
main=r'''
int main(int argc,char **argv) {
    struct TagItem tag;
    FILE *f;
    assert(argc==2);
    snprintf(storage,sizeof(storage),"%s",argv[1]);
    load_default_drawer();load_last_drawer();
    assert(g_default_drawer[0]==0);
    assert(g_last_drawer[0]==0);
    set_initial_music_drawer_tag(&tag);
    assert(tag.ti_Tag==TAG_IGNORE);
    remember_last_drawer("Music:MD");
    set_initial_music_drawer_tag(&tag);
    assert(strcmp((char *)tag.ti_Data,"Music:MD")==0);
    strcpy(selected,"VGM:Arcade");
    choose_default_drawer();
    assert(!strcmp(g_default_drawer,"VGM:Arcade"));
    assert(!strcmp(status,"Default drawer saved"));
    set_initial_music_drawer_tag(&tag);
    assert(tag.ti_Tag==ASLFR_InitialDrawer && !strcmp((char *)tag.ti_Data,"VGM:Arcade"));
    remember_last_drawer("Music:PSG");
    set_initial_music_drawer_tag(&tag);
    assert(!strcmp((char *)tag.ti_Data,"VGM:Arcade"));
    g_default_drawer[0]=0;
    load_default_drawer();
    assert(!strcmp(g_default_drawer,"VGM:Arcade"));
    accept=0;
    strcpy(selected,"Music:MD");
    choose_default_drawer();
    assert(!strcmp(g_default_drawer,"VGM:Arcade"));
    accept=1;
    strcpy(selected,"Missing:Unavailable");
    choose_default_drawer();
    assert(!strcmp(g_default_drawer,"VGM:Arcade"));
    /* Prefer ENVARC when ENV has an outdated entry. */
    f=fopen(mapname(DEFAULT_DRAWER_ENV),"wb");assert(f);
    fputs("Music:MD",f); fclose(f);
    load_default_drawer();
    assert(!strcmp(g_default_drawer,"VGM:Arcade"));
    /* Temporarily missing default falls back to Last. */
    strcpy(g_default_drawer,"Missing:Unavailable");
    set_initial_music_drawer_tag(&tag);
    assert(!strcmp((char *)tag.ti_Data,"Music:PSG"));
    load_default_drawer();
    clear_default_drawer();
    assert(g_default_drawer[0]==0);
    set_initial_music_drawer_tag(&tag);
    assert(!strcmp((char *)tag.ti_Data,"Music:PSG"));
    assert(!Lock(DEFAULT_DRAWER_ENV,ACCESS_READ));
    assert(!Lock(DEFAULT_DRAWER_ENVARC,ACCESS_READ));
    puts("PASS: default/last precedence, persistent reload, directory selection, cancel, missing volume and reset");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='zzvgm_default_test_') as d:
    c=Path(d)/'test.c'; c.write_text(header+core+main)
    exe=Path(d)/'test'; storage=Path(d)/'prefs';storage.mkdir()
    cc=os.environ.get('CC','gcc')
    cmd=[cc,'-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-Wno-error=format-truncation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-o',str(exe),str(c)]
    r=subprocess.run(cmd,text=True,capture_output=True)
    if r.returncode: print(r.stdout+r.stderr);raise RuntimeError('compilation failed')
    r=subprocess.run([str(exe),str(storage)],text=True,capture_output=True)
    if r.returncode: print(r.stdout+r.stderr);raise RuntimeError('tests failed')
    print(r.stdout.strip())
