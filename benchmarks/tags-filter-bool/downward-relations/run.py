import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time


def identity(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path), 'sha256': digest, 'bytes': path.stat().st_size}


root = Path('/tmp/opencode/osmium-issue6-downward-brandenburg')
root.mkdir(exist_ok=False)
previous = json.loads(Path('/tmp/opencode/osmium-dependency-chunks/two-hop-paired/results.json').read_text())
binary = identity('build/src/osmium')
source = identity(previous['source']['path'])
expressions = identity(previous['expressions']['path'])
assert source['sha256'] == previous['source']['sha256']
assert expressions['sha256'] == previous['expressions']['sha256']
env = dict(os.environ, OSMIUM_POOL_THREADS='-2')
orders = [('recursive', 'depth1'), ('depth1', 'recursive'), ('recursive', 'depth1')]
result = {
    'created_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'branch': subprocess.check_output(['git', 'branch', '--show-current'], text=True).strip(),
    'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'working_tree_modified': True,
    'binary': binary,
    'version': subprocess.check_output([binary['path'], '--version'], text=True),
    'source': source, 'expressions': expressions,
    'host': {'platform': platform.platform(), 'logical_cpus': os.cpu_count(),
             'affinity_cpus': len(os.sched_getaffinity(0))},
    'method': {
        'OSMIUM_POOL_THREADS': '-2', 'runs_per_mode': 3,
        'pair_orders': orders, 'warmups': 0,
        'cache': 'No cache flush; input hashing before runs warms the page cache.',
        'wall_time': 'perf_counter; filter process startup through exit, including PBF/TSV output; excludes post-run inspection and hashes.',
        'memory': 'Linux wait4 ru_maxrss: maximum RSS of each individual process, KiB.',
        'cpu_time': 'Linux wait4 user plus system seconds, summed over all threads.',
        'concurrency': 'Filter runs sequential; no concurrent builds or other benchmark jobs launched.',
        'selection': 'Existing Boolean filter, two reverse hops, multipolygon parent predicate, remove dependency-only tags.',
        'only_variable': '--downward-relation-hops=1 present versus absent (recursive).',
        'fsync': False,
    },
    'runs': [],
}
(root / 'tracked-source.diff').write_bytes(subprocess.check_output(['git', 'diff']))
result_path = root / 'results.json'


def save():
    result_path.write_text(json.dumps(result, indent=2) + '\n')


save()
for round_number, order in enumerate(orders, 1):
    for mode in order:
        label = f'{mode}-{round_number}'
        output = root / f'{label}.osm.pbf'
        edges = root / f'{label}.tsv'
        log = root / f'{label}.stderr.log'
        command = [binary['path'], 'tags-filter-bool', source['path'],
                   '-e', expressions['path'], '--include-parents', '--parent-hops=2',
                   '--parents-expression=r/type=multipolygon', '--remove-tags',
                   '--no-progress', '--verbose', '--generator=reverse-reference-exploration',
                   f'--parent-relationships={edges}', '-o', str(output)]
        if mode == 'depth1':
            command.append('--downward-relation-hops=1')
        print(f'Starting {label}', flush=True)
        with (root / f'{label}.stdout.log').open('w') as stdout, log.open('w') as stderr:
            started = time.perf_counter()
            child = subprocess.Popen(command, env=env, stdout=stdout, stderr=stderr)
            _, status, usage = os.wait4(child.pid, 0)
            elapsed = time.perf_counter() - started
            child.returncode = os.waitstatus_to_exitcode(status)
        run = {
            'mode': mode, 'round': round_number, 'command': command,
            'return_code': child.returncode, 'wall_seconds': elapsed,
            'peak_rss_kib': usage.ru_maxrss, 'peak_rss_mib': usage.ru_maxrss / 1024,
            'cpu_user_seconds': usage.ru_utime, 'cpu_system_seconds': usage.ru_stime,
            'cpu_total_seconds': usage.ru_utime + usage.ru_stime,
            'stderr_path': str(log),
        }
        result['runs'].append(run)
        save()
        if child.returncode:
            raise RuntimeError(log.read_text())
        run['output'] = identity(output)
        run['relationships'] = identity(edges)
        run['input_passes'] = int(re.search(r'Needed (\d+) pass', log.read_text()).group(1))
        save()
        print(f"{label}: {elapsed:.3f} s; peak {run['peak_rss_mib']:.2f} MiB; {run['output']['bytes']} output bytes; {run['input_passes']} passes", flush=True)

result['summary'] = {}
for mode in ('recursive', 'depth1'):
    runs = [run for run in result['runs'] if run['mode'] == mode]
    assert len({run['output']['sha256'] for run in runs}) == 1, 'Repeated PBF output differs'
    assert len({run['relationships']['sha256'] for run in runs}) == 1, 'Repeated TSV output differs'
    output = runs[0]['output']
    info = json.loads(subprocess.check_output([binary['path'], 'fileinfo', '-e', '-j', output['path']], env=env, text=True))
    (root / f'{mode}-fileinfo.json').write_text(json.dumps(info, indent=2) + '\n')
    counts = {key: info['data']['count'][key] for key in ('nodes', 'ways', 'relations')}
    summary = {
        'runs': len(runs), 'output': output, 'counts': counts,
        'total_objects': sum(counts.values()), 'input_passes': runs[0]['input_passes'],
    }
    for metric in ('wall_seconds', 'peak_rss_mib', 'cpu_total_seconds'):
        values = [run[metric] for run in runs]
        summary[metric] = {'median': statistics.median(values), 'min': min(values), 'max': max(values), 'values': values}
    result['summary'][mode] = summary

assert len({run['relationships']['sha256'] for run in result['runs']}) == 1, 'Downward depth changed reverse TSV'
recursive = result['summary']['recursive']
bounded = result['summary']['depth1']
result['comparison'] = {
    'wall_reduction_percent': (1 - bounded['wall_seconds']['median'] / recursive['wall_seconds']['median']) * 100,
    'peak_rss_reduction_percent': (1 - bounded['peak_rss_mib']['median'] / recursive['peak_rss_mib']['median']) * 100,
    'output_size_reduction_percent': (1 - bounded['output']['bytes'] / recursive['output']['bytes']) * 100,
    'object_count_reduction_percent': (1 - bounded['total_objects'] / recursive['total_objects']) * 100,
    'removed_counts': {key: recursive['counts'][key] - bounded['counts'][key] for key in recursive['counts']},
    'reverse_relationships_byte_identical': True,
    'recursive_matches_pre_feature_saved_output': recursive['output']['sha256'] == previous['reference_output']['sha256'],
    'reverse_relationships_match_pre_feature_saved_output': result['runs'][0]['relationships']['sha256'] == previous['reference_edges']['sha256'],
}
result['completed_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
save()

lines = [
    '# Brandenburg downward relation depth benchmark', '',
    'Same current executable and workload; only `--downward-relation-hops=1` differs.',
    'Boolean expressions, `--include-parents --parent-hops=2 --parents-expression=r/type=multipolygon --remove-tags`.',
    '`OSMIUM_POOL_THREADS=-2`. Three alternating pairs; medians reported. No cache flush or explicit warmups.',
    'Wall time includes output writing, excludes post-run inspection. Peak memory is per-process Linux maximum RSS.', '',
    '| Metric | Option absent (recursive) | Downward N=1 |',
    '| --- | ---: | ---: |',
]
for title, key, unit in [('Wall time', 'wall_seconds', 's'), ('Peak RSS', 'peak_rss_mib', 'MiB'), ('CPU time', 'cpu_total_seconds', 's')]:
    lines.append(f"| {title}, median | {recursive[key]['median']:.3f} {unit} | {bounded[key]['median']:.3f} {unit} |")
lines.append(f"| PBF bytes | {recursive['output']['bytes']:,} | {bounded['output']['bytes']:,} |")
for key in ('nodes', 'ways', 'relations'):
    lines.append(f"| {key.title()} | {recursive['counts'][key]:,} | {bounded['counts'][key]:,} |")
lines.append(f"| Total objects | {recursive['total_objects']:,} | {bounded['total_objects']:,} |")
lines.append(f"| Input passes | {recursive['input_passes']} | {bounded['input_passes']} |")
lines += ['', '## Individual runs', '', '| Round | Mode | Wall s | Peak RSS MiB | CPU s |', '| --- | --- | ---: | ---: | ---: |']
for run in result['runs']:
    lines.append(f"| {run['round']} | {run['mode']} | {run['wall_seconds']:.3f} | {run['peak_rss_mib']:.3f} | {run['cpu_total_seconds']:.3f} |")
lines += ['', '## Comparison', '', '```json', json.dumps(result['comparison'], indent=2), '```', '',
          'N=1 intentionally omits dependencies below the relation boundary; smaller output is not equivalent completeness.',
          'Three local warm-cache observations are not a general performance guarantee.', '',
          f"Input SHA-256: `{source['sha256']}`; {source['bytes']:,} bytes.",
          f"Expression SHA-256: `{expressions['sha256']}`.",
          f"Executable SHA-256: `{binary['sha256']}`.",
          'Full commands, timings, hashes, host details and identities: `results.json`.', '']
(root / 'report.md').write_text('\n'.join(lines))
print(json.dumps({'summary': result['summary'], 'comparison': result['comparison'], 'report': str(root / 'report.md')}, indent=2), flush=True)
