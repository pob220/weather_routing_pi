# Automatic input settings: full-corpus screening — 13 November 2026

6,072 attempts across 2 candidate comparisons. Each comparison covers all 253 cases under saved inputs and one candidate, with all six frozen engines. The corpus contains 55 real-GRIB cases and 16 negative controls.

These are bounded standalone screens: 5 seconds of solver wall time for analytic cases and 10 seconds for real-GRIB cases. Preparation and replay are separate. Deadline stops need longer follow-up and do not prove infeasibility. Parallel runtime ratios are approximate. Fixture inputs and allowances are preserved; these are not exact GUI Auto runs or Windows measurements. Synthetic manual headings default to 15 degrees, and imported cases include recorded 5- and 10-degree settings.

Quick2 rows are explicitly replaced by complete adapter-revision-2 campaigns, forwarding the nominal step and 4096-layer horizon as the current plugin does. The other five engines retain their original paired trials. Superseded Quick2 control rows remain archived and are excluded from these totals. Source-campaign paths and replacement paths are recorded in `analysis.json`. Atlas and Alternative retain their internal numerical policies.

## duration-coast-v1

| Engine | Saved: validated / 253 | Candidate: validated / 253 | Real GRIB: saved / candidate |
|---|---:|---:|---:|
| Quick | 151 | 10 | 18 / 2 |
| Quick2 | 220 | 213 | 43 / 37 |
| Atlas | 210 | 209 | 34 / 33 |
| Standard | 202 | 188 | 34 / 21 |
| Alternative | 193 | 195 | 33 / 32 |
| Professional | 59 | 9 | 9 / 1 |

226 per-engine completion losses (61 non-deadline stops), 15 gains, and 67 paired passage-duration penalties above 2%. The 2% threshold is an investigation flag.

Process errors: 0; completed-route replay failures: 0; accepted negative controls: 0.

Default qualification remains pending. Inspect `analysis.json` for individual regressions and effective input plans in each attempt record.

## engine-nominal-v2

| Engine | Saved: validated / 253 | Candidate: validated / 253 | Real GRIB: saved / candidate |
|---|---:|---:|---:|
| Quick | 150 | 153 | 20 / 26 |
| Quick2 | 220 | 218 | 43 / 41 |
| Atlas | 210 | 210 | 32 / 32 |
| Standard | 192 | 197 | 30 / 30 |
| Alternative | 190 | 188 | 30 / 31 |
| Professional | 47 | 40 | 8 / 8 |

43 per-engine completion losses (3 non-deadline stops), 40 gains, and 8 paired passage-duration penalties above 2%. The 2% threshold is an investigation flag.

Process errors: 0; completed-route replay failures: 0; accepted negative controls: 0.

Default qualification remains pending. Inspect `analysis.json` for individual regressions and effective input plans in each attempt record.

## Decision boundary

Keep these prototypes outside the plugin. Do not enable a new automatic default from this screening alone. Follow up material completion/quality regressions with longer paired runs, then qualify the selected policy in the production request adapter, arrival/multi-leg workflows and saved/manual-setting UI. Keep the engines’ own local adaptation and all safety/data requirements intact.

## Confirmed follow-ups and development status

A longer Quick follow-up on the real-GRIB `real_pacific_east` case preserves the
same one-million-generated-state fixture ceiling. Saved inputs (three hours,
15-degree headings) complete; the second prototype selects one hour and
10-degree headings and exhausts the generated-state allowance even with a
60-second solver deadline. This is a deliberate count ceiling, not evidence of
exhausted system RAM. That fixture ceiling is not the plugin's universal default.

Quick2 also loses two real-GRIB completions with the second prototype: its
unchanged two-million-proposal allowance is exhausted after the candidate
reduces saved three-/six-hour steps to one hour. Forcing an hourly nominal step
merely because currents are enabled is too crude for ocean passages. A separate
longer Quick replay confirms a 2.24% passage-duration penalty on
`generated_0055`; both results pass independent validation.

Reject `duration-coast-v1`. Keep `engine-nominal-v2` experimental. The next
candidate should distinguish offshore current variability from the simple
Currents checkbox, retain coarse offshore nominal steps where suitable, and
leave local refinement to each engine. Record which controls each engine
actually consumes. Preserve manual inputs independently of the automatic plan.

The candidates are excluded from the plugin. The Automatic adaptive settings
checkbox/default is not implemented or enabled by this development patch.
Production adapter integration, arrival/multi-leg qualification and remembered
manual/automatic UI settings remain to be completed after selecting a policy.

The 2.2.2 development build already contains clearer allowance diagnostics,
calendar/clock entry for passage and GRIB times, and the button label
**Stop exploration - keep results**. Validation includes 688 existing enabled
Linux tests, all 37 updated diagnostic tests, real time widgets under four host
time zones, and the 164-input production-engine freeze check. No production
engine/model input changed. All 86,848 referenced input files match their
pre-run hashes after the campaigns. The protected working executable, wrapper,
desktop launcher and 1.36.1 plugin also match their pre-run hashes.

Full receipts, binary/source snapshots, per-attempt tracks, the corrected
Quick2 campaigns and extended follow-ups are under the workspace's
`output/xweather-2.2.2/`. The public Windows trial and existing user test
installation remain 2.2.1; this is development qualification, not a 2.2.2 release.
