"""Stream the bounded CPU-only H2 region capture into per-second CSV and evidence JSON.

No game/process access, no third-party dependencies. Partial crash tails are
reported, not treated as complete captures. Timings are CPU wall times, NOT GPU
execution times. DrawIndexed counts exclude other APIs and worker contexts.
"""
import argparse
import csv
import json
import math
import struct
from collections import defaultdict, OrderedDict
from pathlib import Path

ROW = struct.Struct('<11Q')
PHASES = {1: 'backend', 2: 'claim', 3: 'clone', 4: 'admission',
          5: 'scene_completion', 6: 'scene_publication',
          10: 'wait_initial', 11: 'wait_surfaces', 12: 'wait_fx', 20: 'surface_producer'}


def loss_windows(path):
    """Conservatively locate loss between cumulative writer health reports.

    A clean stop means a footer exists, not that every event arrived. Without
    invocation IDs a lost begin/end can pair with a recycled record seconds
    later. Never use an interval crossing possible loss as measured work.
    """
    windows = []
    with path.open('rb') as source:
        if source.read(8) != b'H2VREG01':
            raise ValueError('not a supported H2 region capture')
        source.read(24)
        previous_health = None
        previous_drops = 0
        while len(data := source.read(ROW.size)) == ROW.size:
            values = ROW.unpack(data)
            tick, _, kind = values[:3]
            if previous_health is None:
                previous_health = tick
            if kind in (10, 11):
                if values[5] > previous_drops:
                    windows.append((previous_health, tick))
                previous_health, previous_drops = tick, values[5]
    return windows


def analyze(path, output):
    output.mkdir(parents=True, exist_ok=True)
    buckets = defaultdict(lambda: defaultdict(list))
    pending = {}
    markers, anomalies, slow = [], [], []
    dropped = incomplete = total = 0
    clean_stop = False
    first = None
    previous_submit = None
    origin = {}
    losses = loss_windows(path)
    excluded = 0
    rigid_results = OrderedDict()
    surface_budget = {'capacity_bytes': 1 << 18, 'index_unit_bytes': 4, 'maximum_cursor_bytes': 0,
                      'over_limit_observations': 0, 'native_model_failures_over_limit': 0,
                      'generator_draw_types': {}}
    def crosses_loss(begin, end):
        return any(begin <= hi and end >= lo for lo, hi in losses)
    with path.open('rb') as source, (output / 'lists.csv').open('w', newline='', encoding='utf-8') as lists, \
            (output / 'phases.csv').open('w', newline='', encoding='utf-8') as phases, \
            (output / 'rigid_models.jsonl').open('w', encoding='utf-8') as rigid, \
            (output / 'surface_budget.csv').open('w', newline='', encoding='utf-8') as budget:
        list_writer = csv.writer(lists)
        phase_writer = csv.writer(phases)
        budget_writer = csv.writer(budget)
        budget_writer.writerow(['second', 'thread', 'scope', 'id', 'stage', 'frontend',
                                'cursor_bytes', 'capacity_bytes', 'model_count', 'record_index',
                                'draw_type_or_outcome', 'slot_or_entry'])
        phase_writer.writerow(['second', 'thread', 'event', 'phase', 'record_or_predicate',
                               'frontend', 'result', 'flag_BE8', 'flag_BEC', 'flag_BF0', 'flag_BF4',
                               'list24_type', 'list24_owner', 'family'])
        list_writer.writerow(['second', 'pair', 'eye', 'stage', 'list', 'pointer', 'count',
                              'technique', 'descriptor_hash', 'owner_pointer', 'x', 'y', 'z'])
        if source.read(8) != b'H2VREG01':
            raise ValueError('not a supported H2 region capture')
        frequency, pid, unix_ms = struct.unpack('<3Q', source.read(24))
        if not frequency:
            raise ValueError('invalid QPC frequency')
        while data := source.read(ROW.size):
            if len(data) != ROW.size:
                incomplete = len(data)
                break
            tick, thread, kind, pair, eye, a, b, c, d, e, f = ROW.unpack(data)
            if first is None:
                first = tick
            seconds = (tick - first) / frequency
            bucket = buckets[int(seconds)]
            total += 1
            name = None
            key = None
            ending = False
            if kind in (2, 3):
                name, key, ending = 'frontend', (thread, 'frontend'), kind == 3
            elif kind in (4, 5):
                name = f'owner{eye}'
                key, ending = (thread, name, pair), kind == 5
                if ending:
                    bucket[f'draw_indexed{eye}'].append(a)
                    bucket[f'indices{eye}'].append(b)
            elif kind == 1:
                names = {1: 'present', 2: 'present', 3: 'wait_poses', 4: 'wait_poses',
                         5: 'submit', 6: 'submit', 7: 'conversion', 8: 'conversion',
                         9: 'dynamic_upload', 10: 'dynamic_upload'}
                name = names.get(a)
                # Calls on each thread are sequential. Original trace a/b have
                # API-specific meanings; never assume they always identify a pair.
                key, ending = (thread, name), a in (2, 4, 6, 8, 10)
                if a == 6:
                    bucket['submit_calls'].append(1)
                    if eye != 0:
                        anomalies.append({'s': seconds, 'type': 'submit_error', 'result': eye})
                    if pair == 1 and eye == 0:  # successful right Submit
                        if previous_submit is not None:
                            interval = (tick - previous_submit) * 1000 / frequency
                            uncertain = crosses_loss(previous_submit, tick)
                            if uncertain:
                                excluded += 1
                            else:
                                bucket['stereo_submit_interval_ms'].append(interval)
                            if not uncertain and interval >= 33.333 and len(slow) < 20000:
                                slow.append({'s': seconds, 'stage': 'stereo_submit_interval', 'ms': interval})
                        previous_submit = tick
            elif kind == 6:
                list_writer.writerow([seconds, pair, eye, a >> 32, a & 0xffffffff,
                                      hex(b), c, d, hex(e), hex(f), *origin.get((pair, eye), [None]*3)])
                if b == 0 and c and len(anomalies) < 20000:
                    anomalies.append({'s': seconds, 'type': 'null_nonempty_list', 'pair': pair,
                                      'eye': eye, 'stage': a >> 32, 'list': a & 0xffffffff,
                                      'count': c, 'technique': d, 'descriptor_hash': e, 'owner': f})
                if b == 0 and c:
                    bucket['null_nonempty_lists'].append(1)
            elif kind == 7:
                values = [struct.unpack('<f', struct.pack('<I', v))[0] for v in (b, c, d)]
                # Keep only the latest pair per eye, without using stale positions
                # from a different pair or growing memory with capture duration.
                for old_key in [key for key in origin if key[1] == eye]:
                    del origin[old_key]
                origin[pair, eye] = [v if math.isfinite(v) else None for v in values]
            elif kind == 8:
                anomalies.append({'s': seconds, 'type': 'owner_failure', 'pair': pair,
                                  'eye': eye, 'reason': a, 'completed_mask': b})
            elif kind == 9:
                markers.append({'s': seconds, 'label': {1: 'normal', 2: 'slow', 3: 'visual'}.get(a, str(a))})
            elif kind in (10, 11):
                dropped = max(dropped, a)
                clean_stop |= kind == 11
            elif kind in (12, 13, 14):
                phase = PHASES.get(eye, f'phase_{eye}')
                phase_writer.writerow([seconds, thread, {12: 'begin', 13: 'end', 14: 'state'}[kind],
                                       phase, hex(pair), hex(a), b if kind == 13 else '',
                                       *([b & 0xffffffff, b >> 32, c & 0xffffffff, c >> 32,
                                          d, hex(e), f] if kind == 14 else [''] * 7)])
                if kind != 14:
                    name, key, ending = phase, (thread, phase, pair), kind == 13
            elif kind in (15, 16, 17):
                decoded = {'s': seconds, 'thread': thread, 'id': pair}
                if kind == 15:
                    outcome, result = e >> 32, e & 0xffffffff
                    decoded.update(type='build', record=hex(a), entry=hex(b), model=hex(c),
                                   lighting=hex(d), outcome=outcome, native_result=result, owner=hex(f))
                    rigid_results[pair] = (outcome, result)
                    if len(rigid_results) > 4096:
                        rigid_results.popitem(last=False)
                elif kind == 16:
                    decoded.update(type='context', frontend=hex(a), record_index=b,
                                   view_flags=c, model_flags=d, draw_info=e, camera_hash=hex(f))
                else:
                    values = [struct.unpack('<f', struct.pack('<I', v & 0xffffffff))[0]
                              for v in (a, b, c, d, e, f)]
                    values = [v if math.isfinite(v) else None for v in values]
                    decoded.update(type='placement', stage=eye, position=values[:3],
                                   record_model_origin=values[3:])
                rigid.write(json.dumps(decoded, allow_nan=False) + '\n')
            elif kind == 18:
                scope = 'model' if eye == 3 else 'generator'
                budget_writer.writerow([seconds, thread, scope, pair, eye, hex(a), b,
                                        surface_budget['capacity_bytes'], c, d, e, hex(f)])
                surface_budget['maximum_cursor_bytes'] = max(surface_budget['maximum_cursor_bytes'], b)
                over = b > surface_budget['capacity_bytes']
                surface_budget['over_limit_observations'] += int(over)
                if eye == 0:
                    key_type = str(e)
                    types = surface_budget['generator_draw_types']
                    types[key_type] = types.get(key_type, 0) + 1
                elif eye == 3:
                    outcome = rigid_results.pop(pair, None)
                    # An adapter rejection is NOT a native arena failure, even
                    # when a previous model already pushed the cursor over limit.
                    if over and outcome and outcome[0] in (1, 2) and outcome[1] == 0:
                        surface_budget['native_model_failures_over_limit'] += 1
            elif kind == 19:
                if (a, b, c, d) != (524288, 8, 8, 32):
                    raise ValueError('unsupported surface storage layout in capture')
                surface_budget.update(capacity_bytes=a,index_unit_bytes=b)
            if name:
                if not ending:
                    # A new begin after a lost end replaces the orphan, rather
                    # than charging a later frame for an entire missing interval.
                    if kind == 12:
                        # Native waits pump jobs; nested calls on one thread are
                        # legitimate and must not overwrite the outer timestamp.
                        pending.setdefault(key, []).append((tick, pair))
                    else:
                        pending[key] = [(tick, pair)]
                elif stack := pending.get(key):
                    start = stack.pop()
                    if not stack:
                        del pending[key]
                    duration = (tick - start[0]) * 1000 / frequency
                    if duration < 0 or crosses_loss(start[0], tick):
                        excluded += 1
                        continue
                    bucket[name + '_ms'].append(duration)
                    if duration >= 33.333 and len(slow) < 20000:
                        slow.append({'s': seconds, 'stage': name, 'pair_or_trace_a': start[1],
                                     'ms': duration, 'origin': {f'{p}:{eye}': v for (p, eye), v in origin.items()}})
    metrics = sorted({metric for bucket in buckets.values() for metric in bucket})
    fields = ['second'] + [f'{metric}_{stat}' for metric in metrics for stat in ('n', 'mean', 'p95', 'max')]
    with (output / 'timeline.csv').open('w', newline='', encoding='utf-8') as target:
        writer = csv.DictWriter(target, fieldnames=fields)
        writer.writeheader()
        for second, bucket in sorted(buckets.items()):
            record = {'second': second}
            for metric, values in bucket.items():
                values.sort()
                record.update({f'{metric}_n': len(values), f'{metric}_mean': sum(values) / len(values),
                               f'{metric}_p95': values[min(len(values)-1, int(len(values)*.95))],
                               f'{metric}_max': values[-1]})
            writer.writerow(record)
    report = {'pid': pid, 'unix_ms': unix_ms, 'rows': total, 'dropped_reported': dropped,
              'clean_stop': clean_stop, 'trailing_bytes': incomplete,
              'unmatched_begins': sum(len(stack) for stack in pending.values()),
              'timing_intervals_excluded_for_loss': excluded,
              'possible_loss_windows_s': [[(lo-first)/frequency, (hi-first)/frequency] for lo, hi in losses],
              'markers': markers, 'anomalies': anomalies, 'slow_spans': slow,
              'surface_budget': surface_budget,
              'limits': 'CPU wall time only; DrawIndexed on owner thread only. List hashes are CPU descriptors, not shader/texture bindings. Event loss can leave gaps. Lists checked at owner boundaries, not each draw.'}
    (output / 'evidence.json').write_text(json.dumps(report, indent=2, allow_nan=False), encoding='utf-8')
    print(f'{total} rows; drops={dropped}; clean_stop={clean_stop}; output={output}')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    analyze(args.capture, args.output or args.capture.with_suffix(''))
