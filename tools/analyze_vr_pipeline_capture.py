"""Read fresh vr_diagnose snapshots without touching a game or VR runtime.

The reported rates are native Presents and successful OpenXR world submissions,
not HMD display FPS. Frontend admission calls are observations, not frame counts.
"""
import argparse
import json
from pathlib import Path
import re
import zipfile


COUNTERS = ('presents', 'runtime_presents', 'completed', 'world', 'menu_only',
            'empty', 'runtime_failures', 'owner_completed', 'owner_failures')
ADMISSION = ('unarmed', 'accepted', 'invalid_pair', 'production_busy', 'pair_unavailable')
CPU = ('transaction_samples', 'transaction_total_us', 'eye0_samples',
       'eye0_total_us', 'eye1_samples', 'eye1_total_us')
MAX_REPORT_BYTES = 8 * 1024 * 1024  # Matches the support archive's per-file limit.


def fields(text, section):
    matches = re.findall(r'^\s*' + re.escape(section) + r': ([^\r\n]+)$', text, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f'Expected one {section} section; use the prepared capture build')
    return dict(token.split('=', 1) for token in matches[0].split() if '=' in token)


def integer_fields(values, names):
    result = {name: int(values[name]) for name in names}
    if any(value < 0 for value in result.values()):
        raise ValueError('Negative cumulative counter')
    return result


def setting(text, name):
    match = re.search(r'^\s*' + re.escape(name) + r'=(.*)$', text, re.MULTILINE)
    if not match:
        return None
    value = match[1].strip()
    if value.startswith('"'):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value
    return value


def parse_snapshot(text):
    context = fields(text, 'frame_pipeline')
    if context['schema'] != '1':
        raise ValueError('Unsupported frame pipeline snapshot schema')
    for key in ('pid', 'tick_ms', 'snapshot_span_ms', 'session', 'device', 'reference',
                'focused', 'enabled', 'native_ready', 'owner_ready'):
        context[key] = int(context[key])
    scene = re.search(r'\bscene_initialized=(\w+)', text)
    build = re.search(r'^\s*client: .*configuration=(\w+) git_hash=(\S+)', text, re.MULTILINE)
    context.update(map=setting(text, 'mapname'), paused=setting(text, 'cl_paused'),
                   ingame=setting(text, 'cl_ingame'), scene_initialized=scene[1] if scene else None,
                   configuration=build[1] if build else None, revision=build[2] if build else None)
    return {'context': context,
            'counts': integer_fields(fields(text, 'frame_pipeline_counts'), COUNTERS),
            'admission': integer_fields(fields(text, 'frame_pipeline_admission'), ADMISSION),
            'cpu': integer_fields(fields(text, 'frame_pipeline_cpu'), CPU),
            'settings': {name: setting(text, name) for name in
                         ('r_postAA', 'r_vsync', 'com_maxfps', 'com_waitEndFrameMode')}}


def analyze_pair(before, after):
    first, last = before['context'], after['context']
    reasons = []
    for key in ('pid', 'session', 'device', 'reference', 'backend', 'map', 'configuration', 'revision'):
        if first[key] != last[key]:
            reasons.append(key + '_changed')
    for label, context in (('start', first), ('end', last)):
        if context['backend'] != 'openxr':
            reasons.append(label + '_not_openxr')
        if context['state'] != 'running':
            reasons.append(label + '_runtime_not_running')
        for key in ('focused', 'enabled', 'native_ready', 'owner_ready'):
            if context[key] != 1:
                reasons.append(label + '_' + key + '_false')
        if context['paused'] not in ('0', 'no', 'false'):
            reasons.append(label + '_paused_or_unknown')
        # The verified CL_IsCgameInitialized witness is authoritative. Some
        # builds may omit the supplementary cl_ingame dvar from diagnostics.
        if context['scene_initialized'] != 'yes' or context['ingame'] in ('0', 'no', 'false'):
            reasons.append(label + '_gameplay_not_ready')
        if context['map'] in (None, '', 'unavailable', 'unreadable'):
            reasons.append(label + '_map_unknown')
        if not context['configuration'] or not context['revision']:
            reasons.append(label + '_build_unknown')
    if before['settings'] != after['settings']:
        reasons.append('graphics_settings_changed')
    seconds = (last['tick_ms'] - first['tick_ms']) / 1000
    if seconds <= 0:
        reasons.append('nonpositive_window')
    delta = {}
    for group in ('counts', 'admission', 'cpu'):
        delta[group] = {key: after[group][key] - value for key, value in before[group].items()}
        if any(value < 0 for value in delta[group].values()):
            reasons.append(group + '_counter_reset')
    counts = delta['counts']
    if counts['completed'] != counts['world'] + counts['menu_only'] + counts['empty']:
        reasons.append('submission_partition_mismatch')
    for key in ('menu_only', 'runtime_failures', 'owner_failures'):
        if counts[key] > 0:
            reasons.append(key + '_during_window')
    if delta['admission']['unarmed'] > 0:
        reasons.append('owner_unarmed_during_window')
    if counts['world'] <= 0:
        reasons.append('no_world_submissions')
    result = {'usable': not reasons, 'invalid_reasons': reasons, 'seconds': seconds,
              'start': first, 'end': last, 'settings': before['settings'],
              'endpoint_observation_span_ms': [first['snapshot_span_ms'], last['snapshot_span_ms']],
              'delta': delta, 'metrics': None,
              'limits': ['Readiness is sampled at the two endpoints; intermediate state is not continuously observed.',
                         'CPU timings are inclusive wall time; GPU time and HMD display FPS are not measured.']}
    if reasons:
        return result
    cpu = delta['cpu']
    result['metrics'] = {
        'native_presents_hz': counts['presents'] / seconds,
        'runtime_presents_hz': counts['runtime_presents'] / seconds,
        'world_submissions_hz': counts['world'] / seconds,
        'native_presents_per_world': counts['presents'] / counts['world'],
        'runtime_presents_per_world': counts['runtime_presents'] / counts['world'],
        'empty_fraction': counts['empty'] / counts['completed'],
        'owner_wall_us_per_sample': {
            prefix: cpu[prefix + '_total_us'] / cpu[prefix + '_samples']
            if cpu[prefix + '_samples'] else None
            for prefix in ('transaction', 'eye0', 'eye1')},
    }
    return result


def read_bundle(path):
    names = ('status-1.txt', 'status-2.txt')
    if path.is_dir():
        content = []
        for name in names:
            with (path / name).open('rb') as source:
                raw = source.read(MAX_REPORT_BYTES + 1)
            if len(raw) > MAX_REPORT_BYTES:
                raise ValueError(name + ' exceeds the diagnostic file limit')
            content.append(raw.decode('utf-8'))
    else:
        with zipfile.ZipFile(path) as archive:
            content = []
            for name in names:
                found = [entry for entry in archive.infolist()
                         if entry.filename == name or entry.filename.endswith('/' + name)]
                if len(found) != 1 or found[0].file_size > MAX_REPORT_BYTES:
                    raise ValueError('Missing, duplicate or oversized fresh ' + name)
                content.append(archive.read(found[0]).decode('utf-8'))
    return analyze_pair(*(parse_snapshot(text) for text in content))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundles', nargs='+', type=Path, help='Diagnostic ZIPs or their capture folders')
    parser.add_argument('--output', type=Path, help='Optional UTF-8 JSON report')
    args = parser.parse_args()
    reports = []
    for path in args.bundles:
        try:
            reports.append({'source': str(path), **read_bundle(path)})
        except (OSError, ValueError, KeyError, UnicodeError, zipfile.BadZipFile) as error:
            parser.error(f'{path}: {error}')
    output = json.dumps({'schema': 'h2-frame-pipeline-analysis-v1', 'captures': reports},
                        ensure_ascii=False, indent=2, allow_nan=False) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding='utf-8')
    print(output, end='')


if __name__ == '__main__':
    main()
