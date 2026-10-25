"""Build a neutral HDR tracer material from an operator-selected H2 asset dump.

The optional WARP decoder preserves linear HDR values. Source exports and native
readback stay local; ordinary client builds distribute only h2_vr_tracers.ff.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import _winapi
from asset_dump import Reader, Writer
from build_sound_zone import run

ROOT = Path(__file__).resolve().parents[1]
ZONE = 'h2_vr_tracers'
NAME = 'vr_tracer_neutral'


def prepare(source, decoder, output):
    r = Reader(source/'images/tracer#0.h2Image')
    header = bytearray(r.array(104, 1))
    if r.text() != 'tracer#0' or struct.unpack_from('<I', header, 24)[0] != 95 or header[53]:
        raise ValueError('Expected the original resident BC6H_UF16 tracer image')
    width,height = struct.unpack_from('<HH', header, 44)
    mips = header[52]
    if not 0 < width <= 1024 or not 0 < height <= 1024 or not 0 < mips <= 11:
        raise ValueError('Unsupported tracer dimensions')
    compressed = r.array(1); r.finish()
    output.mkdir(parents=True, exist_ok=True)
    raw_source, raw_hdr = output/'source.bc6', output/'source.rgba32f'
    raw_source.write_bytes(compressed)
    subprocess.run([str(decoder), str(raw_source), str(raw_hdr), str(width), str(height), str(mips)], check=True, timeout=60)
    expected = sum(max(1,width>>i)*max(1,height>>i) for i in range(mips))
    decoded = raw_hdr.read_bytes()
    if len(decoded) != expected*16: raise ValueError('Decoded mip extent mismatch')
    pixels = bytearray()
    peak = 0.0
    for red,green,blue,alpha in struct.iter_unpack('<4f', decoded):
        if not all(math.isfinite(v) and 0 <= v <= 65504 for v in (red,green,blue)) or alpha != 1:
            raise ValueError('Invalid original HDR texel')
        # Preserve the peak-channel envelope instead of flattening the image to
        # white or clamping it through an 8-bit/sRGB intermediate.
        intensity = max(red,green,blue)
        peak = max(peak,intensity)
        pixels.extend(struct.pack('<4e',intensity,intensity,intensity,alpha))
    if not peak: raise ValueError('Decoded tracer has no visible texels')
    struct.pack_into('<I',header,24,10)  # R16G16B16A16_FLOAT, same mip chain
    struct.pack_into('<II',header,36,len(pixels),len(pixels))
    struct.pack_into('<Q',header,56,1)
    w=Writer(); w.array(header,104); w.text(NAME); w.array(pixels,1)
    w.save(output/'images'/(NAME+'.h2Image'))
    material=json.loads((source/'materials/fx_tracer.json').read_text('utf-8'))
    if len(material['textureTable'])!=1 or material['textureTable'][0]['image']!='tracer#0':
        raise ValueError('Unexpected original tracer material binding')
    material['name']=NAME; material['textureTable'][0]['image']=NAME
    (output/'materials').mkdir(exist_ok=True)
    (output/'materials'/(NAME+'.json')).write_text(json.dumps(material,indent=2),encoding='utf-8')
    shader=material['techniqueSet->name']
    # Reuse native H2 shader providers and the original state/constant records,
    # as in the melee asset workflow. No shared game material is overwritten.
    for kind in ('state','constantbuffer'):
        destination=output/'techsets'/kind/shader; destination.mkdir(parents=True,exist_ok=True)
        records=list((source/'techsets'/kind/shader).glob('fx_tracer.*'))
        if not records: raise ValueError('Missing native tracer material '+kind)
        for record in records: shutil.copyfile(record,destination/(NAME+record.suffix))
    return {'width':width,'height':height,'mips':mips,'pixels':expected,'hdr_peak':peak,'format':'RGBA16F'}, bytes(pixels)


def build(game, tool, source, decoder, timeout):
    game,tool,source,decoder=(p.resolve(strict=True) for p in (game,tool,source,decoder))
    output=ROOT/'output/tracer-zone'; output.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='h2-tracer-',dir=output)).resolve()
    links=[]
    try:
        report,pixels=prepare(source,decoder,stage/'zonetool'/ZONE)
        for file in game.iterdir():
            if file.is_file() and (file.name in ('MW2CR.exe','.build.info','.product.db') or file.suffix.lower()=='.dll'):
                shutil.copy2(file,stage/file.name)
        (stage/'main').mkdir(); shutil.copyfile(game/'main/fileSysCheck.cfg',stage/'main/fileSysCheck.cfg')
        _winapi.CreateJunction(str(game/'Data'),str(stage/'Data')); links.append(stage/'Data')
        (stage/'zone_source').mkdir(); shutil.copyfile(ROOT/'data/zone_source'/f'{ZONE}.csv',stage/'zone_source'/f'{ZONE}.csv')
        run(tool,stage,'-buildzone',timeout,ZONE)
        compiled=next((p for p in [stage/f'{ZONE}.ff',stage/'zone'/f'{ZONE}.ff'] if p.is_file()),None)
        if not compiled or not compiled.stat().st_size: raise ValueError('No tracer FF emitted')
        run(tool,stage,'-verifyzone',timeout,ZONE)
        run(tool,stage,'-dumpzone',timeout,ZONE)
        dumped=stage/'dump'/ZONE
        r=Reader(dumped/'images'/(NAME+'.h2Image')); h=r.array(104,1)
        if r.text()!=NAME or struct.unpack_from('<I',h,24)[0]!=10 or h[53] or r.array(1)!=pixels:
            raise ValueError('Compiled neutral HDR image differs from prepared mip chain')
        r.finish()
        material=json.loads((dumped/'materials'/(NAME+'.json')).read_text('utf-8'))
        if material['textureTable'][0]['image']!=NAME: raise ValueError('Compiled neutral material binding lost')
        report.update(native_readback=True,bytes=compiled.stat().st_size,stage=str(stage),hmd_verified=False)
        (output/'readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        shutil.copyfile(compiled,ROOT/'data/zone'/f'{ZONE}.ff')
        print(json.dumps(report,indent=2))
    finally:
        if stage.parent!=output.resolve() or not stage.name.startswith('h2-tracer-'): raise ValueError('Unsafe staging boundary')
        for link in links:
            if link.parent!=stage: raise ValueError('Unsafe junction boundary')
            link.rmdir()
        # Keep this bounded private build/readback for diagnosis and provenance.


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-root',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--source-dump',type=Path,required=True,help='Original code_post_gfx dump')
    p.add_argument('--decoder',type=Path,required=True)
    p.add_argument('--timeout',type=int,default=60)
    a=p.parse_args(); build(a.game_root,a.tool,a.source_dump,a.decoder,a.timeout)
