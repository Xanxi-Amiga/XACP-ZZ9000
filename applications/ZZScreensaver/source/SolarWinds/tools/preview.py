#!/usr/bin/env python3
"""Render the scene with Mesa GL, not the FPGA. No network, no Amiga required."""
import ctypes as C
import pathlib, subprocess
from PIL import Image, ImageDraw, ImageFont
P=pathlib.Path(__file__).resolve().parents[1]
B=P/'tests/build';B.mkdir(parents=True,exist_ok=True)
subprocess.run(['gcc','-shared','-fPIC','-O2','-DSW_MESA_PREVIEW',
    '-I'+str(P/'solarwinds'),str(P/'solarwinds/solarwinds.c'),
    '-Wl,-l:libGL.so.1','-lm','-o',str(B/'solarwinds_mesa.so')],check=True)
E=C.CDLL('libEGL.so.1');G=C.CDLL('libGL.so.1')
def api(lib,name,ret,args):
    f=getattr(lib,name);f.restype=ret;f.argtypes=args;return f
ptr=C.c_void_p;I=C.c_int;U=C.c_uint
getproc=api(E,'eglGetProcAddress',ptr,[C.c_char_p])
display=C.CFUNCTYPE(ptr,U,ptr,C.POINTER(I))(getproc(b'eglGetPlatformDisplayEXT'))(0x31DD,None,None)
major,minor=I(),I()
assert api(E,'eglInitialize',U,[ptr,C.POINTER(I),C.POINTER(I)])(display,C.byref(major),C.byref(minor))
assert api(E,'eglBindAPI',U,[U])(0x30A2)
attrs=(I*15)(0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,0,0x3038)
config=ptr();count=I()
assert api(E,'eglChooseConfig',U,[ptr,C.POINTER(I),C.POINTER(ptr),I,C.POINTER(I)])(display,attrs,C.byref(config),1,C.byref(count)) and count.value
pattrs=(I*5)(0x3057,640,0x3056,480,0x3038)
surface=api(E,'eglCreatePbufferSurface',ptr,[ptr,ptr,C.POINTER(I)])(display,config,pattrs)
noattrs=(I*1)(0x3038)
context=api(E,'eglCreateContext',ptr,[ptr,ptr,ptr,C.POINTER(I)])(display,config,None,noattrs)
assert surface and context
assert api(E,'eglMakeCurrent',U,[ptr,ptr,ptr,ptr])(display,surface,surface,context)
renderer=api(G,'glGetString',C.c_char_p,[U])(0x1F01).decode()
read=api(G,'glReadPixels',None,[I,I,I,I,U,U,ptr])
err=api(G,'glGetError',U,[])
sw=C.CDLL(str(B/'solarwinds_mesa.so'))
init=api(sw,'sw_init',I,[U,U]);step=api(sw,'sw_step',None,[])
draw=api(sw,'sw_draw',None,[C.c_float,I])
active=api(sw,'sw_active_particles',U,[])
names=['Regular','Cosmic Strings','Cold Pricklies','Space Fur','Jiggly','Undertow']
out=P/'previews';out.mkdir(exist_ok=True)
canvas=Image.new('RGB',(960,540),(12,17,26));text=ImageDraw.Draw(canvas)
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',14)
for preset,name in enumerate(names,1):
    assert init(preset,0x58414e58)
    for frame in range(240):
        step();draw(1/60,frame==0)
        assert not err(),f'OpenGL error in preset {preset}, frame {frame}'
    data=(C.c_ubyte*(640*480*3))();read(0,0,640,480,0x1907,0x1401,data)
    img=Image.frombytes('RGB',(640,480),bytes(data)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    assert max(img.getextrema()[k][1] for k in range(3))>10,'Blank preview'
    img.save(out/('preset_%d.png'%preset))
    x=((preset-1)%3)*320;y=((preset-1)//3)*270
    canvas.paste(img.resize((320,240),Image.Resampling.LANCZOS),(x,y))
    text.text((x+10,y+245),f'{preset}  {name}',fill=(230,235,245),font=font)
    print(f'{preset} {name}: 240 GL frames; active particles={active()}; no GL errors')
canvas.save(out/'solarwinds_mesa.png')
print('Preview renderer:',renderer)
print('These are Mesa previews of the port. They do not verify FPGA rendering or speed.')
api(E,'eglMakeCurrent',U,[ptr,ptr,ptr,ptr])(display,None,None,None)
api(E,'eglDestroyContext',U,[ptr,ptr])(display,context)
api(E,'eglDestroySurface',U,[ptr,ptr])(display,surface)
api(E,'eglTerminate',U,[ptr])(display)
