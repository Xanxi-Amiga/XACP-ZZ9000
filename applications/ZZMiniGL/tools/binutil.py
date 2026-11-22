from pathlib import Path
import struct
def extract(elf,out):
    b=Path(elf).read_bytes()
    if b[:6]!=b'\x7fELF\x01\x01' or struct.unpack_from('<H',b,18)[0]!=40:raise ValueError('not an ARM ELF32')
    entry,off=struct.unpack_from('<II',b,24)
    size,num=struct.unpack_from('<HH',b,42)
    assert entry==0x04500000
    image=bytearray()
    for i in range(num):
        kind,pos,va,pa,n,mem,flags,align=struct.unpack_from('<8I',b,off+i*size)
        if kind!=1 or not n:continue
        assert va==pa and entry<=pa<=pa+n<=0x04650000 and pos+n<=len(b)
        rel=pa-entry
        if rel+n>len(image):image.extend(bytes(rel+n-len(image)))
        image[rel:rel+n]=b[pos:pos+n]
    assert image and len(image)%2==0
    Path(out).write_bytes(image)
def embed(blob,output):
    data=Path(blob).read_bytes()
    lines=[','.join(str(v) for v in data[i:i+24]) for i in range(0,len(data),24)]
    Path(output).write_text('const unsigned char zz_arm_image[] = {\n'+',\n'.join(lines)+'\n};\nconst unsigned int zz_arm_size = sizeof zz_arm_image;\n')
