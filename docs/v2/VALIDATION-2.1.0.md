# xWeatherRouting 2.1.0 qualification — 8 November 2026

The 2.1 changes are confined to UI presentation, prerequisite messages, setting
help, documentation and the review-bundle workflow. No routing engine, scenario
parser, search parameter, safety algorithm or calculation default is changed.
The [2.0 qualification](VALIDATION-2.0.md) is retained as historical evidence;
it does not replace verification of the new binaries.

## Linux

- Release build and private staged installation passed; **644 enabled CTest
  cases passed**, with the existing live timing comparison still disabled.
- The final UI library passed the relevant diagnostic/readiness/help checks.
  In a private display and profile, the example passage completed. A deliberately
  restrictive wind limit produced the engine's existing failure status; the
  card showed its reason and opened the detailed report. Recalculation after
  switching that limit off completed, displayed **Off** and hid the failure action.
- Separate private host processes completed forecast-only, Climatology-only and
  combined-source passages with the integrated weather providers.
- Comparing 2.0 commit `acd74b8` with this branch found no changes under
  `src/engine`, `src/headless`, `vendor`, `third_party`, or `src/RouteMap.cpp`.
- SHA256 checks confirmed that the working launcher, desktop entry, OpenCPN
  executable and 1.36 plugin were unchanged after testing.

The Linux archive is **53.78 MiB**, containing **134.07 MiB** of installed file
contents: approximately **15 KB compressed / 28 KiB of contents** more than 2.0.
Hosts, profiles, forecasts and generated caches are separate from those figures.

## Windows x64

[Successful native qualification](https://github.com/pob220/xweather_routing_pi/actions/runs/42474024388)
built source `c4e363e` against both pinned OpenCPN hosts: 5.14.2 / wxWidgets
3.2.9 and 5.15.0 / wxWidgets 3.3.4.

- **635 enabled CTest cases passed for each host**; the existing live timing
  comparison remains disabled. Imported host symbols and matching wx SDK/runtime
  files were verified separately from the shared `msvc-64:10` ABI label.
- Both stock hosts imported the package normally. All 792 bundled data assets,
  36 translation catalogs and help were checked. Original, quick and main
  engines completed offshore and shoreline routes; reinstall/restart retained
  personal boat and polar edits.
- Both private portable hosts completed forecast-only, Climatology-only and
  forecast-expiry/fallback routes using the integrated providers.
- The unchanged scientific helper is reused from qualified run `42409887075`
  only after its source/build inputs and staged source hashes match. Its 12
  native unit tests passed in that earlier run. App-local runtime checks ran
  again with each new plugin and archived package, including offline GRIB
  generation/reading with build tools removed from PATH and 36 AMD64 PE images.
- The supplied ZIP's actual launcher passed on native Windows after relocation
  to a folder containing spaces. It opened the passage planner, cleared inherited
  automation overrides and exited cleanly. Normal-profile sentinel files were
  unchanged. The relocated copy completed all three weather-source route cases.
- The delivered ZIP SHA256 and every file in its manifest were verified again
  after downloading the CI artifact. The manifest records the qualified source,
  native run, plugin archive checksum and matching host provenance.

The wx 3.3.4 plugin archive is **36.47 MiB**, with **112.74 MiB** of file
contents. The wx 3.2.9 archive is **36.45 MiB**, with **112.70 MiB** of contents.
The ready-to-run ZIP additionally includes its private OpenCPN host:
**81.64 MiB compressed / 218.14 MiB of file contents**. Test profiles, downloaded
forecasts and generated caches are additional.

Trial ZIP SHA256:

```text
f2908cb06d34d8f17d66ee0b74e6cef3042971efe0ff3396b31f74a9ef7a4862
```

The SDKs, dependency revisions and archive checksums are unchanged. Build-only
retries and a byte-identical direct archive URL handle observed upstream HTTP
502/504 errors; no routing behaviour changes are involved.

Windows display-scaling feedback and custom chart-aware host testing remain
useful. This trial does not replace the stable 1.x catalogue version.
