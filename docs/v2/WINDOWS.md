# xWeatherRouting 2.2.1 on Windows

## Try the new interface — no compiling required

1. [Download the ready-to-run Windows ZIP](https://github.com/pob220/xweather_routing_pi/releases/download/windows-ui-trial-2.2.1/xWeatherRouting-2.2.1-Windows-UI-Trial.zip) (about 82 MiB).
2. Choose **Extract All**, open the extracted folder and double-click
   **Start-Weather-Routing.cmd**.
3. Choose **Passage actions → Try an example passage**, close the help note,
   then click **Calculate route**.

Please tell Paul whether **Route → Boat → Weather** makes sense, what feels
awkward or takes too many clicks, and what is difficult to read. Screenshots
are welcome. This first look is about using the interface; a source build is
optional.

The ZIP includes its own matching OpenCPN test copy. Settings and test files
stay in that extracted folder, leaving your normal OpenCPN alone.
[More trial guidance and feedback suggestions](TRY-WINDOWS.md).

<details>
<summary>Optional: developer source-build instructions</summary>

Use [feature/xweather-2.2.0](https://github.com/pob220/xweather_routing_pi/tree/feature/xweather-2.2.0)
in `pob220/xweather_routing_pi`. This is the
development line; version 1 remains the catalogue release. Native Windows
qualification is recorded in [VALIDATION.md](VALIDATION.md).

## Windows qualification

Version 2.1 is built and tested against both checksum-pinned x64 hosts:

| OpenCPN host | wxWidgets | Artifact name |
| --- | --- | --- |
| 5.15.0, e41f0de | 3.3.4 | xWeatherRouting-2.2.1-windows-x64-wx3.3.4 |
| 5.14.2, de7e706 | 3.2.9 | xWeatherRouting-2.2.1-windows-x64-wx3.2.9 |

[Validation receipts](VALIDATION.md) distinguish the 2.1 checks from the
[earlier 2.0 qualification](VALIDATION-2.0.md). The ready-to-run ZIP is produced
only after both native plugin builds succeed, then checked on a disposable
Windows runner, including its actual launcher and relocated portable profile.

The plugin is approximately **36–37 MiB compressed** and **113 MiB of installed
file contents**. The trial ZIP additionally includes its matching OpenCPN host.
Test profiles, forecasts and generated shoreline caches are additional.

The run's artifacts contain packages, metadata, checksums and test receipts.
Broader Windows UI/DPI and custom chart-aware host review remains useful.
Version 1 remains the stable catalogue release.

## Prerequisites

* Windows x64, Visual Studio 2022 with Desktop development with C++ and a Windows SDK.
* Git, Python 3.11 or newer, CMake 3.20 or newer, 7-Zip (`7z` on PATH).
* GNU gettext (`msgfmt` and `msgmerge` on PATH). CI uses Chocolatey's
  `gettext` package version `1.0.0.20260310`.
* Internet access for the first build and sufficient space for scientific
  dependency builds. Use a short source path, for example `C:\src\xwr`.

Open **x64 Native Tools Command Prompt for VS 2022**:

```bat
git clone --branch feature/xweather-2.2.0 --recurse-submodules https://github.com/pob220/xweather_routing_pi.git C:\src\xwr
cd /d C:\src\xwr
python ci\circleci-build-windows64.py
```

Use the recursive clone above: GitHub's Download ZIP omits `opencpn-libs`.

The script fetches checksum-pinned SDK/host archives, builds import libraries
from the matched x64 host, builds the GRIB helper and plugin separately, runs
their tests, packages them, and calculates an offline route in an extracted
portable OpenCPN. It never runs the OpenCPN installer or launches your normal
OpenCPN executable. First builds take considerably longer than incremental builds.

The default package matches **wxWidgets 3.3.4** and the pinned OpenCPN 5.15.0
test host. For the separately pinned wxWidgets 3.2.9 / OpenCPN 5.14.2 host:

```bat
set WEATHER_ROUTING_WINDOWS_WX_VERSION=3.2.9
python ci\circleci-build-windows64.py
```

Both hosts identify themselves as `msvc-64`; that label alone does not establish
wx ABI compatibility. Choose the package matching the host's wx runtime. The
filenames include `wx334` or `wx329`, and CI checks actual imported symbols.
The SDK JSON files under `ci/` pin exact host/toolkit bytes and revisions.
Other hosts, including chart-aware custom builds, need matching APIs and wx ABI.

## Outputs and testing

* Default artifacts: `artifacts/windows-x64/`; wx 3.2.9 artifacts:
  `compatibility/windows-x64-wx329/`.
* `package/` contains the `.tar.gz`, matching metadata and SHA256SUMS.
* `tests/`, `runtime/`, `packaged-helper-runtime/` and the JSON receipts record
  checks, host loading, routing and the helper's independent runtime.
* `.windows64/launch-test.cmd` (or `.windows64-wx329/launch-test.cmd`) opens the
  extracted test host with a portable profile and the built v2 plugin.

Run the generated launcher to explore Plan passage, boat/polar management,
Climatology, local GRIB loading and the integrated downloader. Forecast
downloads require internet access; the automated helper generation test is
offline and deliberately makes no claim about a live provider's availability.
The portable test host starts without your personal charts or waypoints.
Add test charts/routes there if needed. Do not copy a plugin into a working
installation merely to try the new UI.
The generated profile retains the automated acceptance passages and test boat;
the optional example passage selects the illustrative 40 ft monohull.

Only disposable CI runners also test the normal OpenCPN CLI package import,
first-run data installation, shoreline routes and preservation on reinstall.
That normal-profile test refuses to run outside CI. Developer builds use the
portable test instead.

## Build layout

`.windows-deps/` owns a pinned vcpkg checkout, release-only x64 dependencies,
generator build and stage. The helper carries DLLs beside its executable,
ecCodes GRIB definitions/samples, PROJ data and dependency notices. Tests remove
vcpkg from the helper's PATH and exercise the final archive's runtime again.
Climatology bytes are preserved by `.gitattributes` and remain checksum-verified.

The plugin itself uses the host's pinned wx SDK, without the root vcpkg
wxWidgets manifest. A manual plugin configure must supply
`WEATHER_ROUTING_WINDOWS_GENERATOR_STAGE` pointing at the complete helper stage.
Use the script above for the supported end-to-end build. Windows x86 is not
qualified for this integrated v2 runtime.

</details>
