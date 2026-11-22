"""Generate identical, auditable C-to-Amiga library vector calls for both assemblers."""
from pathlib import Path
# p96LockBitMap and p96UnlockBitMap must be declared HERE, not edited only
# into build/os.S: the Windows/MSYS build regenerates build/os.S every run.
# Classic Picasso96API.library LVOs: -48 (a0/a1/d0), -54 (a0/d0).
calls={
'Sys':{'os_openlib':(552,'a1 d0'),'os_closelib':(414,'a1'),'os_task':(294,'a1'),
'os_find':(276,'a0 a1'),'os_forbid':(132,''),'os_permit':(138,''),'os_cache':(642,'a0 d0 d1'),
'os_alloc':(198,'d0 d1'),'os_free':(210,'a1 d0'),'os_port':(666,''),'os_delport':(672,'a0'),
'os_creq':(654,'a0 d0'),'os_dreq':(660,'a0'),'os_opendev':(444,'a0 d0 a1 d1'),'os_closedev':(450,'a1'),
'os_findport':(390,'a1'),'os_addport':(354,'a1'),'os_remport':(360,'a1'),
'os_setintvec':(162,'d0 a1'),'os_getmsg':(372,'a0'),'os_doio':(456,'a1'),'os_replymsg':(378,'a1'),'os_waitport':(384,'a0'),'os_setsignal':(306,'d0 d1')},
'Int':{'int_openwindow':(204,'a0'),'int_closewindow':(72,'a0'),'int_setpointer':(270,'a0 a1 d0 d1 d2 d3'),'int_clearpointer':(60,'a0'),'int_modifyidcmp':(150,'a0 d0')},
'Exp':{'os_board':(72,'a0 d0 d1')},'Timer':{'clockread':(60,'a0')},
'Dos':{'dos_output':(60,''),'dos_write':(48,'d1 d2 d3'),'dos_delay':(198,'d1'),'dos_open':(30,'d1 d2'),'dos_close':(36,'d1'),
'dos_setvar':(900,'d1 d2 d3 d4'),'dos_getvar':(906,'d1 d2 d3 d4'),'dos_delvar':(912,'d1 d2'),'dos_selectoutput':(882,'d1'),'dos_seek':(66,'d1 d2 d3')},
'P96':{'p96_lock':(48,'a0 a1 d0'),'p96_unlock':(54,'a0 d0'),
'p96_best':(60,'a0'),'p96_open':(90,'a0'),'p96_close':(96,'a0'),
'p96_write':(102,'a0 d0 d1 a1 d2 d3 d4 d5')}}
def generate(root):
 lines=['.text','.even','.globl _os_sys','_os_sys:',' move.l 4.w,%d0',' rts',
        '.globl _get_table','_get_table:',' move.l %a6,-(%sp)',' move.l 8(%sp),%a6',
        ' jsr -30(%a6)',' move.l (%sp)+,%a6',' move.l %d0,%a0',' rts']
 for base,entries in calls.items():
    for name,(off,regs) in entries.items():
        args=regs.split()
        keep=['a6']+[r for r in args if r in ['d2','d3','d4','d5','d6','d7','a2','a3','a4','a5']]
        lines+=['.globl _'+name,'_'+name+':']
        for r in keep:lines.append(f' move.l %{r},-(%sp)')
        lines.append(f' move.l _{base},%a6')
        for i,r in enumerate(args):lines.append(f' move.l {4*(len(keep)+1+i)}(%sp),%{r}')
        lines.append(f' jsr -{off}(%a6)')
        for r in reversed(keep):lines.append(f' move.l (%sp)+,%{r}')
        lines+=[' move.l %d0,%a0',' rts']
 (root/'build/os.S').write_text('\n'.join(lines)+'\n')
