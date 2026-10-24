"""Compile and read back private melee sources using the existing H2 linker.

Ordinary client builds/package copy only data/zone/h2_vr_melee.ff. Source
locations and the external executable are selected by the operator.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
import _winapi
from asset_dump import Reader
from build_sound_zone import run

ROOT=Path(__file__).resolve().parents[1]
ZONE='h2_vr_melee'


def build(game, tool, assets, timeout):
    game,tool,assets=game.resolve(strict=True),tool.resolve(strict=True),assets.resolve(strict=True)
    manifest=json.loads((assets/'assets.json').read_text('utf-8'))
    if len(manifest['models'])!=5 or set(manifest.get('sounds',[]))!={'vr_melee_baton_hit','vr_melee_baton_extend'}:
        raise ValueError('melee source catalog is incomplete')
    for image in (assets/'images').glob('*.h2Image'):
        r=Reader(image);h=r.array(104,1)
        if h[53]:raise ValueError('distributable melee images must not reference source CASC streams')
    output=ROOT/'output/melee-zone';output.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='h2-melee-',dir=output)).resolve()
    links=[]
    try:
        for file in game.iterdir():
            if file.is_file() and (file.name in ('MW2CR.exe','.build.info','.product.db') or file.suffix.lower()=='.dll'):
                shutil.copy2(file,stage/file.name)
        (stage/'main').mkdir();shutil.copyfile(game/'main/fileSysCheck.cfg',stage/'main/fileSysCheck.cfg')
        _winapi.CreateJunction(str(game/'Data'),str(stage/'Data'));links.append(stage/'Data')
        (stage/'zone_source').mkdir();shutil.copyfile(ROOT/'data/zone_source'/f'{ZONE}.csv',stage/'zone_source'/f'{ZONE}.csv')
        shutil.copytree(assets,stage/'zonetool'/ZONE)
        run(tool,stage,'-buildzone',timeout,ZONE)
        compiled=stage/(ZONE+'.ff')
        if not compiled.is_file():compiled=stage/'zone'/(ZONE+'.ff')
        if not compiled.is_file() or not compiled.stat().st_size:raise ValueError('linker emitted no melee FF')
        run(tool,stage,'-verifyzone',timeout,ZONE)
        run(tool,stage,'-dumpzone',timeout,ZONE)
        dumped=stage/'dump'/ZONE
        models={}
        for name in manifest['models']:
            r=Reader(dumped/'xmodel'/(name+'.xmb'));h=r.array(696,1)
            if r.text()!=name or h[9]!=1:raise ValueError('compiled model identity/root mismatch')
            models[name]={'bones':h[8],'surfaces':h[10]}
        for name in manifest['animations']:
            r=Reader(dumped/'xanim'/(name+'.xab'));r.array(216,1)
            if r.text()!=name:raise ValueError('compiled animation identity mismatch')
        for alias in manifest['sounds']:
            raw=json.loads((dumped/'sounds'/(alias+'.json')).read_text('utf-8'))
            if not raw['head'] or raw['aliasName']!=alias:raise ValueError('compiled sound alias missing')
            for entry in raw['head']:
                if entry['flags']&0x21:raise ValueError('baton sounds must retain fixed one-shot attack')
                path=dumped/'loaded_sound'/(entry['soundfile']['name']+'.flac')
                if not path.is_file():raise ValueError('compiled sound recording missing')
        report={'status':'complete','models':models,'animations':len(manifest['animations']),
                'sounds':manifest['sounds'],'ff_bytes':compiled.stat().st_size,
                'native_readback':True,'runtime_admission':False}
        (output/'readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        shutil.copyfile(compiled,ROOT/'data/zone'/(ZONE+'.ff'))
        print(json.dumps(report,indent=2))
    finally:
        if stage.parent!=output.resolve() or not stage.name.startswith('h2-melee-'):
            raise ValueError('unsafe melee staging cleanup')
        for link in links:
            if link.parent!=stage:raise ValueError('unsafe junction cleanup')
            link.rmdir()
        # Keep the bounded native logs/readback after failure for diagnosis.
        # Staged inputs are private and ignored; cleanup is explicit on success.


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-root',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True)
    p.add_argument('--timeout',type=int,default=120)
    a=p.parse_args();build(a.game_root,a.tool,a.assets,a.timeout)
