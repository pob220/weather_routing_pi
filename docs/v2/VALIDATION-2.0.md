# Linux and Windows x64 qualification — 7 November 2026

This records local Linux and native Windows x64 validation of 2.0.0, based on
1.36 commit f2b78a7. It is separate from the stable plugin catalogue.

## Linux checks completed

- Release build against stock API 1.22 headers, retaining the API 1.21 host
  contract; pinned static Jasper; private staged installation and TGZ package.
- 636 enabled CTest tests passed. The existing live ocean timing comparison
  remains disabled. New checks cover independent GRIB loading, failed forecast
  replacement, frame ownership, bundled Climatology, dataset precedence,
  missing masks, worker queries and boat/polar import isolation. The workflow
  refinement adds readiness-policy, recovery, missing-wave, changed-waypoint,
  optional metadata and portable export checks.
- Private OpenCPN profiles with only version 2 enabled completed forecast-only,
  Climatology-only and combined-source routes. A passage extending beyond a
  three-hour forecast completed with reported Climatology use after expiry.
- A chart-aware calculation completed using a private copy of the enhanced
  OpenCPN core and the existing test CM93 collection.
- The packaged generator downloaded a small live NOAA GFS wind GRIB. The
  integrated reader loaded it; the new screen calculated an offshore passage.
- Interactive checks covered route editing, polar library access, request
  prefill, reports, clean shutdown and restoration of passage/weather selection.
  The revised task-labelled workspace also completed a two-leg OpenCPN passage
  and its optional offshore example without separate weather plugins. Metadata
  editing, preference retention, source selection and the checks dialog were
  exercised in a disposable portable profile.
- Hashes of the working launcher, working OpenCPN executable, working desktop
  entry and working 1.36 plugin were unchanged after testing.

## Measured Linux size

The Linux archive is approximately **53.8 MiB**. Installed file contents total
**134.0 MiB**; filesystem allocation on this laptop is approximately **159 MiB**.
The private OpenCPN host, profiles, build directory and downloaded forecasts
are separate from those plugin figures.

| Component | Installed contents |
| --- | ---: |
| Plugin binary | 8.0 MiB |
| Climatology 2026.2 data and provenance | 10.4 MiB |
| Generator runtime, shared libraries and model definitions | 106.7 MiB |
| Other resources and generator executable | 8.9 MiB |

The working 1.36 installation contains 14.7 MiB of files, making the initial
version 2 installation approximately **119.3 MiB larger**. This comparison uses
the actual local builds, not an estimate for every target platform. Most growth
comes from the independent GRIB service. Unused BUFR, aviation and GRIB3
definitions are excluded. Dataset months are loaded lazily with a bounded cache;
installed bytes are not a measure of startup memory consumption.

## Windows x64 checks completed

[Successful native CI run](https://github.com/pob220/xweather_routing_pi/actions/runs/42409887075)
qualified source commit `743c443` against the checksum-pinned OpenCPN 5.15.0 /
wxWidgets 3.3.4 and OpenCPN 5.14.2 / wxWidgets 3.2.9 hosts. Both report the
`msvc-64:10` ABI. SDK/runtime byte checks and actual host/wx imported symbols
were checked separately; that catalogue ABI label alone does not identify wx.

- Both plugin builds passed 627 enabled CTest cases. The existing timing
  comparison remains disabled. The independent GRIB helper passed 12 native
  CTest cases and app-local runtime checks over 36 AMD64 executable/DLL images.
- The helper generated and read GRIB data with only Windows System32 on PATH,
  using bundled DLLs, ecCodes definitions/samples and PROJ data. Checks were
  repeated on the archived package and after normal OpenCPN package import.
- The stock OpenCPN CLI imported each package. All 792 bundled data assets,
  36 translation catalogs and help were verified. Original, quick and main
  engines completed offshore and shoreline routes, followed by a successful
  reinstall/restart preserving personal boat and polar changes.
- Each portable host completed forecast-only, bundled-Climatology-only and
  short-forecast expiry/fallback routes. The latter two explicitly reported
  Climatology wind use. Native tests load the v2 reader directly.
- Private boats, polars, preferences and shoreline caches use the version 2
  namespace. Acceptance found and corrected the inherited shoreline cache
  directory, and wx 3.3 uses the public array API to avoid private STL imports
  absent from the prebuilt SDK.
- All 52 dataset output checksums survived a simulated Windows autocrlf
  checkout, and native CMake retained the strict dataset checksum checks.

| Windows wx SDK | Compressed archive | Installed file contents |
| --- | ---: | ---: |
| 3.3.4 | 36.47 MiB | 112.73 MiB |
| 3.2.9 | 36.44 MiB | 112.69 MiB |

These measurements exclude the host, source/build/dependency trees,
downloaded forecasts and generated cache files. Instructions and artifact names
are in [WINDOWS.md](WINDOWS.md). Developer scripts extract portable hosts; normal
installation tests are restricted to disposable CI runners.

## Ready-to-run Windows UI trial

[Native trial acceptance](https://github.com/pob220/xweather_routing_pi/actions/runs/42423542646)
verified the exact delivered ZIP and its `Start-Weather-Routing.cmd` launcher,
after extraction into a moved folder containing spaces. The planner opened
with the expected Route, Boat and Weather controls, inherited automation/data
overrides were cleared, and a simulated normal OpenCPN profile was preserved.
The relocated host completed forecast, Climatology and fallback routes.

The trial redistributes the already-qualified `743c443` plugin binaries with
matching OpenCPN 5.15.0, app-local VC runtimes, a fresh portable profile and
short review instructions. No compilation or installation is required by the
reviewer. Installer utilities and uninstallers are excluded.

This **complete trial ZIP** is 81.63 MiB compressed / 218.12 MiB of file contents,
including OpenCPN; it is a different measurement from the plugin-only archives
above. Its SHA256 is `0a7874f71ea29cebf814b335c2576363b1c95fcf0f17b120b2b71cb219aed43a`.
See [the trial guide](TRY-WINDOWS.md) for the public download and requested feedback.

## Remaining qualification

macOS dependency packaging, Android's generator adapter and touch workspace,
Windows x86, additional/custom hosts, Windows UI/DPI review and broader live
passage/provider testing remain before catalogue publication. Windows generator
checks here are offline; Linux additionally exercised a live NOAA request.
The desktop generator and advanced editors retain parts of their existing
interfaces. Qualification does not claim the complete UI redesign is finished.
