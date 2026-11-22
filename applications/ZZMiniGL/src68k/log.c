#include "os.h"
/* A7: a Workbench process has no output stream (Output() = 0): nothing is written then */
void zz_log(const char *s){Z n=0,fh;if(!Dos)return;fh=dos_output();if(!fh)return;while(s[n])n++;dos_write(fh,s,n);}
static void number(const char *s,Z n,Z base){
 char v[12];static const char hex[]="0123456789abcdef";unsigned i=11;
 v[i]=0;do{v[--i]=hex[n%base];n/=base;}while(n);
 zz_log(s);zz_log("=");zz_log(v+i);zz_log("\n");
}
void zz_num(const char *s,Z n){number(s,n,10);}
void zz_hex(const char *s,Z n){number(s,n,16);}
