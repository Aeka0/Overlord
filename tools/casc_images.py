"""Read only recorded H2 image slices and retain the original BC mip data."""
from __future__ import annotations
import ctypes
from pathlib import Path
import struct
import lz4.block
from asset_dump import Reader, Writer

LIMIT=64*1024*1024


class Storage:
    def __init__(self, library, root):
        self.library=ctypes.CDLL(str(Path(library).resolve()))
        self.library.open_casc.argtypes=[ctypes.c_wchar_p]
        self.library.open_casc.restype=ctypes.c_void_p
        self.library.close_casc.argtypes=[ctypes.c_void_p]
        self.library.read_casc_slice.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint64,ctypes.c_uint,ctypes.c_void_p]
        self.library.read_casc_slice.restype=ctypes.c_uint
        self.handle=self.library.open_casc(str(Path(root).resolve()))
        if not self.handle:raise ValueError('CASC storage could not be opened')

    def close(self):
        if self.handle:self.library.close_casc(self.handle);self.handle=None

    def read(self, name, offset, count):
        if not 0<count<=LIMIT:raise ValueError('CASC slice exceeds bounded image budget')
        buffer=ctypes.create_string_buffer(count)
        for prefix in ('','main/','zone/'):
            error=self.library.read_casc_slice(self.handle,(prefix+name).encode('ascii'),offset,count,buffer)
            if not error:return buffer.raw
        raise ValueError(f'CASC slice failed: {name}, error={error}')


def decompress(data):
    if len(data)<20 or data[11]!=4:raise ValueError('unsupported native image block compression')
    expected=struct.unpack_from('<I',data)[0]
    if not 0<expected<=LIMIT:raise ValueError('image allocation exceeds bound')
    offset=12;out=bytearray()
    while offset<len(data):
        if offset>len(data)-8:raise ValueError('truncated image block header')
        packed,expanded=struct.unpack_from('<II',data,offset);offset+=8
        if not 0<packed<=len(data)-offset or not 0<expanded<=expected-len(out):raise ValueError('invalid image block size')
        out.extend(lz4.block.decompress(data[offset:offset+packed],uncompressed_size=expanded))
        offset=(offset+packed+3)&~3
    if len(out)!=expected:raise ValueError('native image length mismatch')
    return out


def make_resident(source, destination, storage):
    r=Reader(source);h=bytearray(r.array(104,1));name=r.text()
    resident=r.array(1) if struct.unpack_from('<Q',h,56)[0] else b''
    if not h[53]:
        r.finish();Path(destination).write_bytes(Path(source).read_bytes());return
    streams=[]
    for i in range(4):
        raw=r.array(24,1)
        localized,index,begin,end=struct.unpack('<HH4xQQ',raw)
        width,height,levels=struct.unpack_from('<HHI',h,64+i*8)
        if not begin and not end:continue
        if not 0<begin<end or not width or not height or localized:raise ValueError('invalid or localized image stream')
        pixels=decompress(storage.read(f'imagefile{index}.pak',begin,end-begin))
        streams.append((width,height,levels>>26,pixels))
    r.finish()
    if not streams:raise ValueError('streamed image has no native pixels')
    streams.sort(key=lambda x:x[0]*x[1],reverse=True)
    width,height,levels,_=streams[0]
    pixels=b''.join(s[3] for s in streams)
    fmt=struct.unpack_from('<I',h,24)[0]
    block=8 if fmt in (70,71,72,79,80,81) else 16 if fmt in (73,74,75,76,77,78,82,83,84,94,95,96,97,98,99) else 0
    if not block:raise ValueError('streamed image is not an admitted native BC format')
    expected=0;w,he=width,height;count=0
    while True:
        expected+=max(1,(w+3)//4)*max(1,(he+3)//4)*block;count+=1
        if w==he==1:break
        w=max(1,w//2);he=max(1,he//2)
    if len(pixels)!=expected or levels!=count:raise ValueError('native mip-chain extent mismatch')
    struct.pack_into('<II4HBB',h,36,len(pixels),len(pixels),width,height,1,1,count,0)
    struct.pack_into('<Q',h,56,1);h[64:96]=bytes(32)
    writer=Writer();writer.array(h,104);writer.text(name);writer.array(pixels,1);writer.save(destination)
