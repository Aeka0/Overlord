"""Check production feature entry points in a finished Windows client/PDB pair.

Unlike policy-only tests, this detects functional components accidentally compiled
out of an optimized client and mutable state placed in read-only storage. It
neither loads the game nor executes its hooks.
"""
import argparse
import ctypes
from ctypes import wintypes as W
import json
from pathlib import Path
import struct

FEATURES = {
    'native menu ownership and input': 'vr::native_menu::component::post_unpack',
    'native menu draw provenance': 'vr::native_menu::*render_primitive',
    'native menu cached draw provenance': 'vr::native_menu::*render_cached',
    'spatial menu presentation': 'vr::menu_overlay::presenter::update',
    'spatial menu blur mask': 'vr::menu_backdrop::component::post_unpack',
    'native loading movie presentation': 'vr::menu_overlay::presenter::capture_movie',
    'independent menu canvas': 'vr::spatial_panel::renderer::draw_canvas',
    'native script HUD image ownership': 'vr::native_waypoints::*observe_script_image_layout',
    'trainer sidearm query adapter': 'vr::gameplay::trainer::component::post_unpack',
    'trainer sidearm query handler': 'vr::gameplay::trainer::*current_weapon',
    'trainer switch hint completion': 'vr::gameplay::trainer::*complete_hint',
    'trainer pickup free look': 'vr::gameplay::sequences::trainer::observe',
    'physical pickup surface samples': 'vr::gameplay::weapons::native_carry::pickup_surface',
    'driver weapon interactions': 'vr::gameplay::vehicles::component::post_unpack',
    'driver native script adapter': 'vr::gameplay::vehicles::native::component::post_unpack',
    'driver weapon presentation': 'vr::gameplay::vehicles::presentation_component::post_unpack',
    'head-mounted nightvision gestures': 'vr::gameplay::equipment::nightvision::component::post_unpack',
    'waist quick reload': 'vr::gameplay::weapons::quick_reload::component::post_unpack',
    'optional pose stabilization': 'vr::stabilization::component::post_unpack',
    'native HUD capture': 'vr::native_hud_capture::component::post_unpack',
    'weapon HUD composition': 'vr::gameplay::weapon_hud::component::post_unpack',
    'weapon HUD source': 'vr::gameplay::weapon_hud::source_component::post_unpack',
    'narrative UI': 'vr::narrative_ui::component::post_unpack',
    'directional indicators': 'vr::directional_ui::component::post_unpack',
    'native waypoints': 'vr::native_waypoints::component::post_unpack',
    'native script HUD text ownership': 'vr::native_waypoints::*script_hud_stub',
    'native death quote ownership': 'vr::native_waypoints::*dead_quote_stub',
    'native fullscreen blur per eye': 'vr::native_fullscreen_blur::apply',
    'scripted body arm filtering': 'vr::gameplay::sequences::body::apply',
    'independent hand skin visibility': '*viewmodel_visibility::*skin_stub',
    'rigid action partition assets': 'vr::gameplay::weapons::physical_reload::partition_assets::refresh',
    'cover push judgement view': 'vr::gameplay::weapons::physical_reload::debug::component::post_unpack',
}

# v142 Release builds retained writes while promoting mutable publication
# buffers into .rdata. Beta 3 failed at the carry model frame; Beta 4 exposed the
# same failure in the output-merger report and a read-only backend-target report.
# Function presence alone cannot detect these defects. Release now disables WPO
# for all build inputs; keep these artifact checks alongside semantic readback.
MUTABLE_STORAGE = {
    'weapon carry model publication': 'vr::gameplay::weapons::carry::render_models',
    'backend target report publication': 'vr::engine_stereo_backend_target::published_report',
    'output merger report publication': 'vr::engine_stereo_output_merger::published_report',
}


def image_sections(image: Path) -> list[dict]:
    with image.open('rb') as stream:
        if stream.read(2) != b'MZ':
            raise ValueError('Expected a Windows client image')
        stream.seek(0x3c)
        stream.seek(struct.unpack('<I', stream.read(4))[0])
        if stream.read(4) != b'PE\0\0':
            raise ValueError('Invalid PE signature')
        header = stream.read(20)
        machine, count = struct.unpack_from('<HH', header)
        optional_size = struct.unpack_from('<H', header, 16)[0]
        if machine != 0x8664 or not 0 < count <= 96:
            raise ValueError('Expected an x64 client with a valid section table')
        stream.seek(optional_size, 1)
        sections = []
        for _ in range(count):
            record = stream.read(40)
            name, size, address = struct.unpack_from('<8sII', record)
            flags = struct.unpack_from('<I', record, 36)[0]
            sections.append({'name': name.rstrip(b'\0').decode('ascii'),
                             'begin': address, 'end': address + size,
                             'writable': bool(flags & 0x80000000)})
        return sections


class Symbol(ctypes.Structure):
    _fields_ = [('SizeOfStruct', W.ULONG), ('TypeIndex', W.ULONG),
                ('Reserved', ctypes.c_ulonglong * 2), ('Index', W.ULONG), ('Size', W.ULONG),
                ('ModBase', ctypes.c_ulonglong), ('Flags', W.ULONG), ('Value', ctypes.c_ulonglong),
                ('Address', ctypes.c_ulonglong), ('Register', W.ULONG), ('Scope', W.ULONG),
                ('Tag', W.ULONG), ('NameLen', W.ULONG), ('MaxNameLen', W.ULONG), ('Name', ctypes.c_char * 1)]


def inspect(image: Path) -> dict:
    image = image.resolve()
    if not image.is_file() or not image.with_suffix('.pdb').is_file():
        raise FileNotFoundError('The built client and its same-name PDB are required')
    sections = image_sections(image)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = W.HANDLE
    process = kernel.GetCurrentProcess()
    dbg = ctypes.WinDLL('dbghelp', use_last_error=True)
    dbg.SymSetOptions.argtypes = [W.DWORD]
    dbg.SymInitializeW.argtypes = [W.HANDLE, W.LPCWSTR, W.BOOL]
    dbg.SymInitializeW.restype = W.BOOL
    dbg.SymLoadModuleExW.argtypes = [W.HANDLE, W.HANDLE, W.LPCWSTR, W.LPCWSTR,
                                   ctypes.c_ulonglong, W.DWORD, ctypes.c_void_p, W.DWORD]
    dbg.SymLoadModuleExW.restype = ctypes.c_ulonglong
    callback_type = ctypes.WINFUNCTYPE(W.BOOL, ctypes.POINTER(Symbol), W.ULONG, ctypes.c_void_p)
    dbg.SymEnumSymbols.argtypes = [W.HANDLE, ctypes.c_ulonglong, ctypes.c_char_p, callback_type, ctypes.c_void_p]
    dbg.SymEnumSymbols.restype = W.BOOL
    dbg.SymCleanup.argtypes = [W.HANDLE]
    # UNDNAME, FAIL_CRITICAL_ERRORS, EXACT_SYMBOLS, NO_PROMPTS. A mismatched PDB
    # must fail instead of letting another build's symbols pass the audit.
    dbg.SymSetOptions(0x2 | 0x200 | 0x400 | 0x80000)
    if not dbg.SymInitializeW(process, str(image.parent), False):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        base = dbg.SymLoadModuleExW(process, None, str(image), None, 0x160000000, 0, None, 0)
        if not base:
            raise ctypes.WinError(ctypes.get_last_error())
        def lookup(pattern, tag=5):
            matches = []
            def found(pointer, size, context):
                info = pointer.contents
                # Require emitted functions/data, not declarations or eliminated code.
                if info.Tag == tag and info.Size and info.Address > base:
                    name = ctypes.string_at(ctypes.addressof(info) + Symbol.Name.offset, info.NameLen).decode('utf-8', 'replace')
                    match = {'name': name, 'address': hex(info.Address), 'size': info.Size}
                    if tag == 7:  # SymTagData
                        rva = info.Address - base
                        section = next((s for s in sections
                                        if s['begin'] <= rva and rva + info.Size <= s['end']), None)
                        match.update(section=section['name'] if section else None,
                                     writable=bool(section and section['writable']))
                    matches.append(match)
                return True
            callback = callback_type(found)
            if not dbg.SymEnumSymbols(process, base, pattern.encode(), callback, None):
                raise ctypes.WinError(ctypes.get_last_error())
            return matches
        result = {feature: lookup(pattern) for feature, pattern in FEATURES.items()}
        storage = {name: lookup(pattern, tag=7) for name, pattern in MUTABLE_STORAGE.items()}
        return {'image': str(image), 'features': result,
                'missing': [name for name, matches in result.items() if not matches],
                'mutable_storage': storage,
                'invalid_storage': [name for name, matches in storage.items()
                                    if len(matches) != 1 or not matches[0]['writable']]}
    finally:
        dbg.SymCleanup(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = inspect(args.image)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2), encoding='utf-8')
    for name, matches in result['features'].items():
        print(('PASS' if matches else 'MISSING') + ': ' + name)
    for name, matches in result['mutable_storage'].items():
        valid = name not in result['invalid_storage']
        print(('PASS' if valid else 'INVALID STORAGE') + ': ' + name
              + ' ' + json.dumps(matches))
    raise SystemExit(1 if result['missing'] or result['invalid_storage'] else 0)


if __name__ == '__main__':
    main()
