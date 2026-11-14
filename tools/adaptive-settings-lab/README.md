# Automatic input settings qualification

This separate executable links the current, frozen production solvers. Nothing
here enters the plugin build or registers an engine. It reuses the established
253-case offline corpus, including real GRIBs/polars, GSHHG shorelines, immutable
exported chart evidence and negative controls. Historical fixture adapters are
copied into `support/`; the engine libraries and chart evaluator come from this
checkout, not the older research engine variants.

Build:

```sh
cmake -S tools/adaptive-settings-lab -B /tmp/adaptive-lab -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/adaptive-lab -j 4
python3 tools/adaptive-settings-lab/campaign.py \
  --binary /tmp/adaptive-lab/adaptive-lab \
  --cases /home/paul/src/OpenCPN/output/weather-routing-v2/inputs-final/cases.json \
  --output /tmp/adaptive-screen --workers 6 --timeout 5 --real-timeout 10
cmake -P test/FrozenEngines2_2.cmake
```

Requires C++20, ecCodes, `grib_get`, jsoncpp and wxWidgets development files.
The corpus and local chart/forecast files are external benchmark inputs, not
redistributed with this tool. The launcher fingerprints all referenced input
files, snapshots the executable and records source hashes. A `STOP` file in the
output directory stops scheduling new work; rerunning with the same inputs,
binary, runner and deadlines resumes completed records. Do not run two launchers
against the same output directory simultaneously.

The first candidate, `duration-coast-v1`, is deliberately a hypothesis, not a
recommended default. It chooses a nominal time step aiming at about 120 layers
using boat performance and direct passage duration, limits it around interior
coast probes and enabled currents, and caps it using the GRIB's shortest wind
frame interval. It selects ten-degree headings and five-degree refinement.
Manual runs retain the original fixture's inputs. Standard's separate offshore
and heading options are recorded explicitly and passed through its public API;
its built-in endpoint/coastal stepping remains intact. The first prototype
must reveal where separate engine policies are needed.

The second candidate, `engine-nominal-v2`, keeps at least a one-hour nominal
step for engines that already refine their local steps, chooses longer offshore
steps for longer estimated passages, and leaves Standard's three-hour offshore
policy in place. It still chooses ten-degree headings. Select it with
`--policies manual engine-nominal-v2`; it is also an unqualified experiment.

The synthetic fixture's implicit manual heading is 15 degrees, while imported
cases retain their recorded heading (including 5 and 10 degrees). These are
comparisons against the established corpus inputs, not a claim that every
manual baseline uses the plugin's fresh-user defaults. Read the recorded
effective input plan before interpreting an individual improvement/regression.

Adapter revision 2 forwards Quick2's nominal step and 4096-layer horizon exactly
as the current plugin does. The historical research harness omitted these
options. Earlier Quick2 rows are controls, not an automatic-step test, and must
be replaced by explicitly identified corrected trials when combining results.
Atlas and Alternative retain their own numerical policies; do not interpret
recorded general request fields as effective controls for those engines.

Every case runs under both input policies and all six engines, using fresh
processes, identical safety/data requirements and identical resource ceilings.
Only nominal numerical inputs change. Engine algorithms, count/memory budgets,
recovery logic and internal adaptation remain unchanged. Complete routes get
another chronological replay with fresh, uncached weather/performance/land
wrappers over the same immutable source data. Replay failures and accepted
negative controls are explicit failures, not successes.

This initial pass is a **bounded screening**, not an exact reproduction of GUI
Auto or evidence of infeasibility when a solver stops. Its limits apply to
solver wall time; loading GRIBs/chart evidence and independent replay have a
separate outer watchdog. Parallel timing is approximate. Investigate completion
losses and passage-time penalties with longer, repeated paired runs before
choosing a default. Report per-engine effective inputs, validated completion,
elapsed time, work counters, peak process RSS, route duration and distance.

The result directory contains an input receipt, full per-attempt records with
tracks, compact JSONL results and a summary of completion gains/losses, replay
failures, negative acceptances and passage penalties above two percent. That
two-percent threshold is an investigation flag, not an established quality
guarantee. Existing saved routes and the working OpenCPN profile are never
opened for writing.
