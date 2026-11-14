# Standalone engine lab

This directory is excluded from the plugin build and engine registration. It
reads the frozen 2.1.3 engine sources; it does not patch them or change Auto/All.

`Atlas2Test-first-valid` is a **policy experiment**, using Atlas's existing
`improvementAttempts = 0` option. It returns the first independently validated
candidate instead of spending the default 200 edge solves improving each pass.
It is not Rick's proposed algorithm and is not a new production engine.

Build and run from the repository root:

```sh
cmake -S tools/engine-lab -B /tmp/xweather-engine-lab -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/xweather-engine-lab
/tmp/xweather-engine-lab/atlas2test tools/engine-lab/cases.json > /tmp/atlas2test.json
cmake -P test/FrozenEngines2_2.cmake
```

Requires C++20, CMake, Ninja and jsoncpp development files. The runner creates
fresh providers for each attempt, alternates policy order over three repeats,
uses identical requests, constraints and resource allowances, and independently
replays every complete route with fresh providers. Output includes status,
first-valid and total elapsed time, passage duration and work counters. A replay
failure makes the program fail.

The cases use an analytic weather field, synthetic polar, synthetic islands and
fixed departure time. They cover open water, tacking, a long ocean passage,
changing wind, island detour, the date line, excessive wind and coincident
endpoints. They are controlled experiments, not a reproduction of Rick's boat,
Climatology dataset, GRIB or coastline. No route is a navigation recommendation.
Coincident-endpoint handling in the plugin's readiness checks remains separate
from this direct solver test.

Initial Linux Release results (median of three repeats, 2026-11-13):

| Case | Atlas default ms | First-valid ms | Passage change |
|---|---:|---:|---:|
| Short open water | 148.31 | 0.39 | unchanged |
| Upwind tacking | 160.67 | 1.22 | +13 s |
| Long ocean | 455.32 | 103.31 | +2,932 s (+0.61%) |
| Changing wind | 508.11 | 326.74 | unchanged |
| Island detour | 380.84 | 4.00 | +2,513 s (+9.94%) |
| Date line | 200.16 | 2.19 | unchanged |

All 36 complete routes passed fresh chronological replay. Both policies returned
`search_incomplete` for all six excessive-wind runs and all six coincident-point
runs. Short timings are sensitive to host load; the work/quality tradeoff matters
more than these ratios. The experiment does not establish a remedy for Rick's
multi-minute production runs. Its detour penalty argues against promoting it
without representative real-world inputs and a clear first-result/quality policy.

Issue #33 remains observational evidence, not an algorithm specification. Read
`docs/v2/ISSUE-TRIAGE-2.2.md` for the limits of its timing evidence. Any further
experiment must live here (or another standalone directory), keep independent
validation, and pass the production engine freeze check.
