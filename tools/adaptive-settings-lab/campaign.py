#!/usr/bin/env python3
"""Paired, resumable screening of nominal inputs on frozen production solvers.

Each attempt is a fresh process. This is not the GUI's Auto dispatcher and
bounded failures do not prove infeasibility. No downloads or profile writes.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import platform
import random
import shutil
import statistics
import subprocess
import time
from pathlib import Path

ENGINES = ['Quick', 'Quick2', 'Atlas', 'Standard', 'Alternative', 'Professional']
POLICIES = ['manual', 'duration-coast-v1']
ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write(path, value):
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    temp.replace(path)


def cadence(path):
    # Header-only ecCodes access. Do not infer usable route coverage from this.
    run = subprocess.run(['grib_get', '-w', 'shortName=10u/10v', '-p',
                          'validityDate,validityTime', str(path)],
                         text=True, capture_output=True, timeout=90, check=True)
    from datetime import datetime, timezone
    times = set()
    for line in run.stdout.splitlines():
        day, clock = line.split()
        times.add(datetime.strptime(day + clock.zfill(4), '%Y%m%d%H%M')
                  .replace(tzinfo=timezone.utc).timestamp())
    times = sorted(times)
    return int(min((b-a for a, b in zip(times, times[1:])), default=0))


def summarize(rows, cases):
    by = {(r['case'], r['display_engine'], r['policy']): r for r in rows}
    output = {'attempts': len(rows), 'expected_attempts': len(cases)*len(ENGINES)*2,
              'process_errors': [r['job'] for r in rows if 'process_error' in r],
              'completed_replay_failures': [r['job'] for r in rows
                                          if r.get('complete') and not r.get('valid')],
              'negative_acceptances': [r['job'] for r in rows
                                      if r.get('expected_negative') and r.get('valid')],
              'engines': {}, 'completion_regressions': [], 'completion_gains': [],
              'quality_regressions_over_2_percent': []}
    for engine in ENGINES:
        subset = [r for r in rows if r['display_engine'] == engine]
        info = {}
        for policy in POLICIES:
            selected = [r for r in subset if r['policy'] == policy]
            valid = [r for r in selected if r.get('valid')]
            info[policy] = {'attempts': len(selected), 'validated': len(valid),
                            'validated_real_grib': sum(r.get('real_grib', False) for r in valid),
                            'solver_seconds': sum(r.get('seconds', 0) for r in selected),
                            'median_peak_rss_mib': statistics.median(
                                [r.get('peak_rss_kib', 0)/1024 for r in selected] or [0])}
        ratios, costs = [], []
        for case in cases:
            pair = [by.get((case['name'], engine, p)) for p in POLICIES]
            if any(r is None for r in pair):
                continue
            manual, auto = pair
            key = {'case': case['name'], 'engine': engine}
            if manual.get('valid') and not auto.get('valid'):
                output['completion_regressions'].append(key)
            if auto.get('valid') and not manual.get('valid'):
                output['completion_gains'].append(key)
            if all(r.get('valid') for r in pair):
                ratio = auto['arrival_seconds']/manual['arrival_seconds']
                ratios.append(ratio)
                costs.append(auto['seconds']/max(.000001, manual['seconds']))
                if ratio > 1.02:
                    output['quality_regressions_over_2_percent'].append(
                        dict(key, passage_ratio=ratio))
            if ('request_limits' in manual and 'request_limits' in auto and
                    manual['request_limits'] != auto['request_limits']):
                raise RuntimeError(f'Input policy changed resource limits: {key}')
        info['paired_valid'] = len(ratios)
        info['median_passage_ratio'] = statistics.median(ratios) if ratios else None
        info['worst_passage_ratio'] = max(ratios, default=None)
        info['median_runtime_ratio'] = statistics.median(costs) if costs else None
        output['engines'][engine] = info
    return output


def main():
    global POLICIES, ENGINES
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--real-timeout', type=float, default=10)
    parser.add_argument('--workers', type=int, default=6)
    parser.add_argument('--filter', default='')
    parser.add_argument('--policies', nargs=2, default=POLICIES,
                        choices=['manual', 'duration-coast-v1', 'engine-nominal-v2'])
    parser.add_argument('--engines', nargs='+', default=ENGINES, choices=ENGINES)
    args = parser.parse_args()
    if args.policies[0] != 'manual' or args.policies[1] == 'manual':
        parser.error('--policies must be manual followed by one automatic candidate')
    POLICIES, ENGINES = args.policies, args.engines
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [c for c in json.loads(args.cases.read_text()) if args.filter in c['name']]
    if len({c['name'] for c in cases}) != len(cases):
        raise ValueError('Duplicate case names')
    receipt_path = args.output/'receipt.json'
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text())
        if (receipt['corpus_sha256'] != digest(args.cases) or
            receipt['binary_sha256'] != digest(args.binary) or
            receipt['runner_sha256'] != digest(__file__) or
            receipt['timeout'] != args.timeout or receipt['real_timeout'] != args.real_timeout or
            receipt['policy'] != POLICIES or receipt['engines'] != ENGINES):
            raise ValueError('Resume provenance mismatch; use a new output directory')
        cases = json.loads((args.output/'cases.json').read_text())
    else:
        assets = {c[k] for c in cases for k in ('grib', 'polar', 'shoreline') if k in c}
        for folder in {c['chart_tiles'] for c in cases if 'chart_tiles' in c}:
            assets.add(str(Path(folder)/'identity.json'))
            assets.update(str(p) for p in (Path(folder)/'tiles').glob('*.json'))
        print(f'Fingerprinting {len(cases)} cases and {len(assets)} input files', flush=True)
        input_hashes = {p: digest(p) for p in sorted(assets)}
        cadences = {p: cadence(p) for p in {c['grib'] for c in cases if 'grib' in c}}
        for case in cases:
            if 'grib' in case:
                case['weather_cadence_s'] = cadences[case['grib']]
        receipt = dict(corpus=str(args.cases.resolve()), corpus_sha256=digest(args.cases),
                       binary_sha256=digest(args.binary), runner_sha256=digest(__file__),
                       timeout=args.timeout, real_timeout=args.real_timeout,
                       workers=args.workers, machine=platform.platform(), inputs=input_hashes,
                       cases=len(cases), real_grib_cases=sum('grib' in c for c in cases),
                       negative_controls=sum(c.get('expected_negative', False) for c in cases),
                       policy=POLICIES, engines=ENGINES, started=time.time(),
                       scope='Current frozen solvers with existing standalone fixture contracts; not GUI Auto')
        receipt['source_sha256'] = {str(p.relative_to(ROOT)): digest(p)
                                  for folder in ('vendor', 'tools/adaptive-settings-lab')
                                  for p in (ROOT/folder).rglob('*') if p.is_file()
                                  and p.suffix in ('.h', '.cpp', '.txt', '.py')}
        shutil.copytree(Path(__file__).parent, args.output/'source')
        shutil.copy2(args.binary, args.output/'adaptive-lab')
        write(args.output/'cases.json', cases)
        write(receipt_path, receipt)
    binary = (args.output/'adaptive-lab').resolve()
    if digest(binary) != receipt['binary_sha256']:
        raise ValueError('Binary snapshot changed')
    for name in ('scenarios', 'records', 'errors'):
        (args.output/name).mkdir(exist_ok=True)
    jobs = []
    for index, case in enumerate(cases):
        for engine in ENGINES:
            order = POLICIES if (index+ENGINES.index(engine)) % 2 == 0 else list(reversed(POLICIES))
            for policy in order:
                key = f'{index:04d}-{engine}-{policy}'
                scenario = dict(case, input_policy=policy)
                path = args.output/'scenarios'/f'{key}.json'
                write(path, scenario)
                jobs.append((key, case, engine, policy, path))
    # Pair members stay adjacent, but case/engine pair order is deterministic
    # and shuffled to avoid warming only one policy's inputs first.
    pairs = [jobs[i:i+2] for i in range(0, len(jobs), 2)]
    random.Random(20261113).shuffle(pairs)
    jobs = [job for pair in pairs for job in pair]
    started = time.monotonic()

    def attempt(job):
        key, case, engine, policy, path = job
        record_path = args.output/'records'/f'{key}.json'
        if record_path.exists():
            return json.loads(record_path.read_text())
        if (args.output/'STOP').exists():
            return None
        timeout = args.real_timeout if 'grib' in case else args.timeout
        then = time.monotonic()
        row = dict(case=case['name'], display_engine=engine, policy=policy, job=key,
                   expected_negative=case.get('expected_negative', False), real_grib='grib' in case)
        try:
            process = subprocess.run(['nice', '-n', '10', str(binary), engine, str(path.resolve()),
                                      str(timeout), '1'], capture_output=True, text=True,
                                     timeout=timeout+180)
            if process.returncode:
                raise RuntimeError(f'Exit {process.returncode}: {process.stderr[-4000:]}')
            payload = json.loads(process.stdout)
            row.update(payload)
            if payload.get('case') != case['name'] or payload.get('display_engine') != engine:
                raise ValueError('Mismatched result identity')
        except Exception as error:
            row.update(process_error=str(error), valid=False, complete=False)
            (args.output/'errors'/f'{key}.txt').write_text(str(error)+'\n')
        row['process_seconds'] = time.monotonic()-then
        write(record_path, row)
        return row

    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
        for row in executor.map(attempt, jobs):
            if row is None:
                continue
            rows.append(row)
            if len(rows) % 24 == 0:
                print(f'{len(rows)}/{len(jobs)} attempts; elapsed {time.monotonic()-started:.0f}s; '
                      f'validated {sum(r.get("valid", False) for r in rows)}; '
                      f'errors {sum("process_error" in r for r in rows)}', flush=True)
                write(args.output/'progress.json', summarize(rows, cases))
    rows.sort(key=lambda r: r['job'])
    with (args.output/'results.jsonl').open('w') as stream:
        for row in rows:
            stream.write(json.dumps({k:v for k,v in row.items() if k != 'track'})+'\n')
    summary = summarize(rows, cases)
    summary['elapsed_wall_s'] = time.monotonic()-started
    summary['full_coverage'] = len(rows) == len(jobs)
    write(args.output/'summary.json', summary)
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
