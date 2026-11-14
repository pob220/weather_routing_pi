#!/usr/bin/env python3
"""Compare completed screening campaigns without treating deadlines as infeasibility."""
import argparse
import collections
import json
import statistics
from pathlib import Path


def analyze(folder, correction=None):
    receipt = json.loads((folder/'receipt.json').read_text())
    summary = json.loads((folder/'summary.json').read_text())
    if not summary['full_coverage']:
        raise ValueError(f'Incomplete screening: {folder}')
    rows = [json.loads(line) for line in (folder/'results.jsonl').open()]
    if correction:
        corrected_receipt = json.loads((correction/'receipt.json').read_text())
        corrected_summary = json.loads((correction/'summary.json').read_text())
        for field in ('corpus_sha256', 'inputs', 'policy', 'timeout', 'real_timeout'):
            if receipt[field] != corrected_receipt[field]:
                raise ValueError(f'Quick2 correction differs in {field}')
        if (corrected_receipt['engines'] != ['Quick2'] or
                not corrected_summary['full_coverage']):
            raise ValueError('Quick2 correction is incomplete')
        vendor = lambda r: {p:h for p,h in r['source_sha256'].items() if p.startswith('vendor/')}
        if vendor(receipt) != vendor(corrected_receipt):
            raise ValueError('Quick2 correction changed engine sources')
        rows = [r for r in rows if r['display_engine'] != 'Quick2']
        replacements = [json.loads(line) for line in (correction/'results.jsonl').open()]
        if any(r.get('adapter_revision') != 2 for r in replacements):
            raise ValueError('Quick2 replacement does not use corrected adapter')
        rows.extend(replacements)
    key = lambda r: (r['case'], r['display_engine'], r['policy'])
    by = {key(r): r for r in rows}
    if len(by) != len(rows) or len(rows) != summary['expected_attempts']:
        raise ValueError('Missing or duplicate attempts')
    manual, candidate = receipt['policy']
    result = dict(candidate=candidate, attempts=len(rows),
                  process_errors=[r['job'] for r in rows if 'process_error' in r],
                  replay_failures=[r['job'] for r in rows if r.get('complete') and not r.get('valid')],
                  negative_acceptances=[r['job'] for r in rows if r.get('expected_negative') and r.get('valid')],
                  engines={}, completion_losses=[], completion_gains=[], quality_flags=[])
    result['source_campaign'] = str(folder.resolve())
    result['quick2_replacement_campaign'] = str(correction.resolve()) if correction else None
    any_completed = collections.defaultdict(set)
    for engine in receipt['engines']:
        pairs = [(r, by[(r['case'], engine, candidate)]) for r in rows
                 if r['display_engine'] == engine and r['policy'] == manual]
        ratios, runtime_ratios = [], []
        for old, new in pairs:
            for row in (old, new):
                if row.get('valid'):
                    any_completed[row['policy']].add(row['case'])
            if old.get('valid') and not new.get('valid'):
                result['completion_losses'].append(dict(case=old['case'], engine=engine,
                    status=new.get('status'), reason=new.get('message'),
                    deadline_stop=new.get('status') == 'cancelled'))
            if new.get('valid') and not old.get('valid'):
                result['completion_gains'].append(dict(case=old['case'], engine=engine))
            if old.get('valid') and new.get('valid'):
                ratio = new['arrival_seconds']/old['arrival_seconds']
                ratios.append(ratio)
                if old['seconds'] > .01 and new['seconds'] > .01:
                    runtime_ratios.append(new['seconds']/old['seconds'])
                if ratio > 1.02:
                    result['quality_flags'].append(dict(case=old['case'], engine=engine,
                                                       passage_ratio=ratio))
        result['engines'][engine] = dict(
            cases=len(pairs), saved_valid=sum(a.get('valid', False) for a,b in pairs),
            candidate_valid=sum(b.get('valid', False) for a,b in pairs),
            saved_real_valid=sum(a.get('valid', False) and a['real_grib'] for a,b in pairs),
            candidate_real_valid=sum(b.get('valid', False) and b['real_grib'] for a,b in pairs),
            paired_valid=len(ratios), median_passage_ratio=statistics.median(ratios) if ratios else None,
            worst_passage_ratio=max(ratios, default=None),
            approximate_median_runtime_ratio=statistics.median(runtime_ratios) if runtime_ratios else None)
    result['any_engine_completed_cases'] = {p: len(any_completed[p]) for p in receipt['policy']}
    result['new_whole_case_losses'] = sorted(any_completed[manual]-any_completed[candidate])
    result['new_whole_case_gains'] = sorted(any_completed[candidate]-any_completed[manual])
    result['default_qualified'] = False
    result['scope'] = 'Bounded standalone screening; not GUI Auto, Windows timing, or a feasibility proof'
    return receipt, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaigns', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--quick2-corrections', nargs='+', type=Path)
    args = parser.parse_args()
    corrections = args.quick2_corrections or [None]*len(args.campaigns)
    if len(corrections) != len(args.campaigns):
        parser.error('Supply one Quick2 correction per campaign')
    outputs, vendor = [], None
    for folder, correction in zip(args.campaigns, corrections):
        receipt, result = analyze(folder, correction)
        current = {p:h for p,h in receipt['source_sha256'].items() if p.startswith('vendor/')}
        if vendor is not None and vendor != current:
            raise ValueError('Campaigns did not use identical vendor engine sources')
        vendor = current
        outputs.append(result)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'analysis.json').write_text(json.dumps(outputs, indent=2)+'\n')
    text = ['# Automatic input settings: full-corpus screening', '',
            f'{sum(o["attempts"] for o in outputs):,} attempts across {len(outputs)} candidate comparisons. '
            'Each comparison covers all 253 cases under saved inputs and one candidate, '
            'with all six frozen engines. The corpus contains 55 real-GRIB cases and '
            '16 negative controls.', '',
            'These are bounded standalone screens: 5 seconds of solver wall time for '
            'analytic cases and 10 seconds for real-GRIB cases. Preparation and replay '
            'are separate. Deadline stops need longer follow-up and do not prove '
            'infeasibility. Parallel runtime ratios are approximate. Fixture inputs '
            'and allowances are preserved; these are not exact GUI Auto runs or '
            'Windows measurements. Synthetic manual headings default to 15 degrees, '
            'and imported cases include recorded 5- and 10-degree settings.', '']
    if any(corrections):
        text += ['Quick2 rows are explicitly replaced by complete adapter-revision-2 '
                 'campaigns, forwarding the nominal step and 4096-layer horizon as '
                 'the current plugin does. The other five engines retain their '
                 'original paired trials. Superseded Quick2 control rows remain '
                 'archived and are excluded from these totals. Source-campaign paths '
                 'and replacement paths are recorded in `analysis.json`. Atlas and '
                 'Alternative retain their internal numerical policies.', '']
    for out in outputs:
        text += [f'## {out["candidate"]}', '',
                 '| Engine | Saved: validated / 253 | Candidate: validated / 253 | Real GRIB: saved / candidate |',
                 '|---|---:|---:|---:|']
        for engine, info in out['engines'].items():
            text.append(f'| {engine} | {info["saved_valid"]} | {info["candidate_valid"]} | '
                        f'{info["saved_real_valid"]} / {info["candidate_real_valid"]} |')
        losses = out['completion_losses']
        hard = sum(not row['deadline_stop'] for row in losses)
        text += ['', f'{len(losses)} per-engine completion losses ({hard} non-deadline stops), '
                 f'{len(out["completion_gains"])} gains, and {len(out["quality_flags"])} paired '
                 'passage-duration penalties above 2%. The 2% threshold is an investigation flag.', '',
                 f'Process errors: {len(out["process_errors"])}; completed-route replay failures: '
                 f'{len(out["replay_failures"])}; accepted negative controls: '
                 f'{len(out["negative_acceptances"])}.', '',
                 'Default qualification remains pending. Inspect `analysis.json` for individual '
                 'regressions and effective input plans in each attempt record.', '']
    text += ['## Decision boundary', '',
             'Keep these prototypes outside the plugin. Do not enable a new automatic default '
             'from this screening alone. Follow up material completion/quality regressions '
             'with longer paired runs, then qualify the selected policy in the production '
             'request adapter, arrival/multi-leg workflows and saved/manual-setting UI. '
             'Keep the engines’ own local adaptation and all safety/data requirements intact.', '']
    (args.output/'report.md').write_text('\n'.join(text))
    print(json.dumps({o['candidate']: dict(attempts=o['attempts'], losses=len(o['completion_losses']),
          gains=len(o['completion_gains']), quality_flags=len(o['quality_flags'])) for o in outputs}, indent=2))
    if any(o['process_errors'] or o['replay_failures'] or o['negative_acceptances'] for o in outputs):
        raise SystemExit(2)


if __name__ == '__main__':
    main()
