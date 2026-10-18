from pathlib import Path
s=Path('amiga/ZZVGMPlay.c').read_text()
assert 'Set Default Drawer...' in s
assert 'Clear Default Drawer' in s
assert s.count('GAD_DEFAULT_SET')==0
assert s.count('GAD_DEFAULT_CLEAR')==0
assert 'IDCMP_MENUPICK' in s
assert 'CreateMenusA(g_main_menu_defs,NULL)' in s
assert 'LayoutMenusA(g_main_menu,g_vi,NULL)' in s
assert 'SetMenuStrip(g_win,g_main_menu)' in s
assert s.count('ClearMenuStrip(g_win)')==2
assert 'FreeMenus(g_main_menu)' in s
assert s.count('choose_default_drawer();')==1
assert s.count('clear_default_drawer();')==1
assert 'set_initial_music_drawer_tag(&tags[4])' in s
assert 'DEFAULT_DRAWER_ENVARC' in s
assert 'LAST_DRAWER_ENVARC' in s
assert 'win_h=btn_y+btn_h+g_scr->WBorBottom+4;' in s
assert 'ZZVGMPlay v1.0 - (c) Xanxi 2026' in s
print('All menu/persistence/source regression checks passed')
