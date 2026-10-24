"""Prepare private H1/H2 source exports for the standalone H2 melee zone.

All source locations are operator inputs. Original exported geometry and
curves remain outside Git; the client distributes only the compiled FF.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import shutil
import struct

from asset_dump import Reader, Writer
from casc_images import Storage, make_resident


def h1_model(source, destination, h2_template):
    r, w = Reader(source), Writer()
    h = r.array(680, 1)
    n, roots, surfaces = h[8:11]
    if (not 0 < n <= 255 or roots != 1 or h[11] or h[13] or h[514]
            or struct.unpack_from('<H', h, 590)[0]
            or struct.unpack_from('<H', h, 632)[0]):
        raise ValueError('H1 model requires unsupported reactive/physics/composite/blendshape adaptation')
    target = bytearray(696)
    target[8:14] = bytes((n, roots, surfaces, h[12], 0, 0))
    # This H2-only scalar has no H1 counterpart. Preserve the native H2
    # first-person knife provider's value instead of inventing a new default.
    target[16:20] = h2_template[16:20]
    target[20:24] = h[16:20]
    target[24:56] = h[20:52]
    for i in range(6):
        target[120+i*72:176+i*72] = h[128+i*64:184+i*64]
    for dest, start, count in ((552,512,2),(556,516,4),(576,528,4),
                               (580,544,28),(624,632,2),(626,590,6),
                               (664,636,4),(668,584,4)):
        target[dest:dest+count] = h[start:start+count]
    w.array(target, 696)
    w.text(r.text())
    w.array(r.array(4, n), 4)
    for _ in range(n):
        w.text(r.text())
    for stride, count in ((1,n-roots),(8,n-roots),(12,n-roots),(1,n),(32,n)):
        w.array(r.array(stride, count), stride)
    if r.array(16):
        raise ValueError('unexpected H1 reactive motion data')
    for stride in (16,36,28,2,8):
        w.array(r.array(stride), stride)
    for _ in range(surfaces):
        w.text(r.text(True), True)
    for _ in range(6):
        w.text(r.text(True), True)
        w.array(b'', 32)
    w.text(r.text(True), True)
    w.text(r.text(True), True)
    weights = struct.unpack_from('<H', h, 592)[0]
    w.array(r.array(4, weights), 4)
    for _ in range(weights):
        w.text(r.text())
    for stride in (8,40):
        w.array(r.array(stride), stride)
    w.array(r.array(8), 8)
    w.text(r.text(True), True)
    w.array(r.array(32), 32)
    r.finish()
    w.save(destination)


def resident_dds(source, destination, name, semantic=0):
    data = source.read_bytes()
    if len(data) < 148 or data[:4] != b'DDS ' or data[84:88] != b'DX10':
        raise ValueError('source must be a bounded DX10 DDS')
    fmt = struct.unpack_from('<I', data, 128)[0]
    height, width = struct.unpack_from('<II', data, 12)
    mip = struct.unpack_from('<I', data, 28)[0]
    if not 0 < width <= 8192 or not 0 < height <= 8192 or not 1 <= mip <= 14:
        raise ValueError('invalid DDS extent')
    payload = data[148:]
    h = bytearray(104)
    struct.pack_into('<I', h, 24, fmt)
    h[28], h[29], h[31] = 3, semantic, 0x40
    struct.pack_into('<II4HBB', h, 36, len(payload), len(payload), width, height, 1, 1, mip, 0)
    struct.pack_into('<Q', h, 56, 1)
    w = Writer()
    w.array(h, 104)
    w.text(name)
    w.array(payload, 1)
    w.save(destination)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--h1-extraction', type=Path, required=True)
    p.add_argument('--h2-dump', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--game-root', type=Path, required=True)
    p.add_argument('--casc-reader', type=Path, required=True)
    a = p.parse_args()
    h1, h2, out = a.h1_extraction.resolve(), a.h2_dump.resolve(), a.output.resolve()
    for source in (h1,h2,a.game_root.resolve()):
        if out.is_relative_to(source) or source.is_relative_to(out):
            raise ValueError('prepared output must be separate from every source installation/export root')
    provenance=json.loads((h1/'manifest.json').read_text('utf-8'))
    if provenance.get('status')!='complete' or provenance.get('source_mode')!='multiplayer' or provenance.get('tool_revision')!='3802db4':
        raise ValueError('H1 source must be the complete pinned MP extraction, not an unverified layout')
    out.mkdir(parents=True, exist_ok=False)
    for folder in ('xmodel','xsurface','xanim','images','materials'):
        (out/folder).mkdir()
    # H2's own models/materials remain exact H2 data. H1 models are converted
    # field-for-field using the pinned H2->H1 converter's inverse contract.
    models = [('wpn_h1_melee_baton_vm', 'h1'), ('wpn_h1_melee_karambit_vm', 'h1'),
              ('h2_viewmodel_knife','common'), ('viewmodel_commando_knife','ending'),
              ('viewmodel_commando_knife_bloody','ending')]
    h2_template=Reader(h2/'ending/xmodel/viewmodel_commando_knife.xmb').array(696,1)
    for name, zone in models:
        if zone == 'h1':
            h1_model(h1/'raw/xmodel'/(name+'.xmb'), out/'xmodel'/(name+'.xmb'),h2_template)
        else:
            shutil.copyfile(h2/zone/'xmodel'/(name+'.xmb'), out/'xmodel'/(name+'.xmb'))
    # XSurface layouts/protocol are shared by the upstream H2->H1 NO_CONVERT
    # adapter. Copy only exact LOD0 sources referenced by the selected models.
    surfaces = [('853b3072_abdc48ad_100','h1'), ('e37dbc13_d51fc566_100','h1'),
                ('wpn_h2_melee_commando_knife_lodvm100','ending')]
    ghost = Reader(h2/'common/xmodel/h2_viewmodel_knife.xmb')
    gh = ghost.array(696,1)
    ghost.text()
    ghost.array(4)
    for _ in range(gh[8]): ghost.text()
    for stride in (1,8,12,1,32,16,36,28,2,8): ghost.array(stride)
    ghost_materials = [ghost.text(True) for _ in range(gh[10])]
    surfaces.append((ghost.text(True),'common'))
    for name, zone in surfaces:
        source = h1/'raw/xsurface'/(name+'.xsb') if zone=='h1' else h2/zone/'xsurface'/(name+'.xsb')
        shutil.copyfile(source, out/'xsurface'/source.name)
    # Keep only the used H2 shader families. Assets are selected recursively by
    # the zone linker; unrelated extracted shaders are never added to the FF.
    # Shader code is obtained from H2's already loaded native common assets by
    # the linker. Only the selected material's state/constant records are local
    # inputs; copying the whole extracted campaign shader tree is unnecessary.
    material_names = ghost_materials + ['m/mtl_h2_weapon_commando_knife','m/mtl_h2_weapon_commando_knife_bloody']
    for name in set(material_names):
        source = next((h2/z/'materials'/(name+'.json') for z in ('techsets_common','techsets_ending')
                       if (h2/z/'materials'/(name+'.json')).is_file()),None)
        if not source: raise ValueError('missing H2 material: '+name)
        target = out/'materials'/(name+'.json');target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)
        shader=json.loads(source.read_text('utf-8'))['techniqueSet->name']
        zone_root=source.parents[len(Path(name).parts)]
        for kind in ('state','constantbuffer'):
            for record in (zone_root/'techsets'/kind/shader).glob(name+'.*'):
                destination=out/'techsets'/kind/shader/record.relative_to(zone_root/'techsets'/kind/shader)
                destination.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(record,destination)
    template_name = 'm/mtl_h2_weapon_commando_knife'
    template = json.loads((out/'materials'/(template_name+'.json')).read_text('utf-8'))
    shader = template['techniqueSet->name']
    white = 'overlord_melee_white'
    for name in ('m/mtl_wpn_h1_baton_01','m/mtl_wpn_h1_melee_karambit_01'):
        original = json.loads((h1/'raw/materials'/(name+'.json')).read_text('utf-8'))
        material = copy.deepcopy(template)
        material['name'] = name
        source_textures = {t['semantic']:t['image'] for t in original['textureTable']}
        for texture in material['textureTable']:
            if texture['semantic'] in (2,5,8): texture['image'] = source_textures[texture['semantic']]
            elif texture['semantic']==9: texture['image'] = white
            elif texture['image'].endswith('_occ-l'): texture['image'] = white
        target = out/'materials'/(name+'.json');target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(json.dumps(material,indent=2),encoding='utf-8')
        material_names.append(name)
        for kind in ('state','constantbuffer'):
            directory = out/'techsets'/kind/shader
            for source in list(directory.glob(template_name+'.*')):
                target = directory/name
                target = target.with_suffix(source.suffix)
                target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(source,target)
    image_names = set()
    storage=Storage(a.casc_reader,a.game_root)
    for name in set(material_names):
        material = json.loads((out/'materials'/(name+'.json')).read_text('utf-8'))
        for texture in material['textureTable']:
            image = texture['image']
            if image in image_names: continue
            image_names.add(image)
            h1_dds = h1/'exported/textures'/image/'texture.dds'
            target = out/'images'/(image.replace('*','_')+'.h2Image')
            if h1_dds.is_file():
                resident_dds(h1_dds,target,image,texture['semantic'])
            elif image==white:
                h=bytearray(104);struct.pack_into('<I',h,24,28);h[28]=3;h[29]=texture['semantic'];h[31]=0x40
                struct.pack_into('<II4HBB',h,36,4,4,1,1,1,1,1,0);struct.pack_into('<Q',h,56,1)
                w=Writer();w.array(h,104);w.text(image);w.array(bytes([255]*4),1);w.save(target)
            else:
                source=next((h2/z/'images'/target.name for z in ('ending','common','code_post_gfx')
                             if (h2/z/'images'/target.name).is_file()),None)
                if not source: raise ValueError('missing H2 image: '+image)
                make_resident(source,target,storage)
    storage.close()
    animations = set()
    for source in (h1/'raw/xanim').glob('*.xab'):
        if source.stem.startswith(('h1_wpn_melee_baton_','h1_wpn_melee_karambit_')):
            shutil.copyfile(source,out/'xanim'/source.name);animations.add(source.stem)
    for weapon in ('ending_knife','alt_ending_knife','ending_knife_bloody','alt_ending_knife_bloody'):
        data=json.loads((h2/'ending/weapons'/(weapon+'.json')).read_text('utf-8'))
        for table in ('szXAnims','szXAnimsRightHanded','szXAnimsLeftHanded'):
            for name in data.get(table,{}).values():
                if not name or name in animations:continue
                source=next((h2/z/'xanim'/(name+'.xab') for z in ('ending','common','code_post_gfx')
                             if (h2/z/'xanim'/(name+'.xab')).is_file()),None)
                if not source:raise ValueError('missing exact animation: '+name)
                shutil.copyfile(source,out/'xanim'/source.name);animations.add(name)
    # Common H2 hand pose animations are explicit regular-weapon assets.
    for name in ('h2_wpn_knife_melee_slice','h2_wpn_knife_melee_stab'):
        source=next((h2/z/'xanim'/(name+'.xab') for z in ('common','ending','code_post_gfx')
                     if (h2/z/'xanim'/(name+'.xab')).is_file()),None)
        if not source:raise ValueError('missing common knife animation: '+name)
        shutil.copyfile(source,out/'xanim'/source.name);animations.add(name)
    manifest={'models':[n for n,_ in models],'materials':sorted(set(material_names)),
              'images':sorted(image_names),'animations':sorted(animations),
              'source_contract':'H1 3802db4 -> H2 native XModel; shared XSurface/XAnim; H2 material shader provider',
              'runtime_admission':False}
    (out/'assets.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps({key:len(value) for key,value in manifest.items() if isinstance(value,list)}))


if __name__=='__main__':main()
