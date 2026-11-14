# xWeatherRouting 2.2.1

Version 2.2 is developed on `feature/xweather-2.2.0`, following 2.1.3. Version 1
remains the stable catalogue and maintenance line. Version 2 uses separate
preferences and user data.

See [2.2 changes](CHANGES-2.2.md) and [the issue triage](ISSUE-TRIAGE-2.2.md).
This pass refines the interface and retains run diagnostics. The production
engine implementations and solver policies remain unchanged; performance experiments
are separate standalone executables. New passages default to Auto with every
engine checked for Auto and All; saved mode and membership choices take precedence. The coincident-waypoint checks from
[2.1.3](CHANGES-2.1.3.md), Unicode correction and GRIB timestamp fix remain.

For Windows UI review, use the [portable trial](TRY-WINDOWS.md), which includes
its matching OpenCPN host.

The desktop workspace groups passage, boat and weather preparation, without
experience levels. Its settings areas are labelled Passage settings and Routing
controls. The redundant name/version heading below the workspace tabs is removed.

Readiness is evaluated outside the widgets using structured route diagnostics.
Each card has a textual state and a checks dialog provides recovery actions;
unknown weather fields and preliminary coverage estimates remain explicit.
The weather selector distinguishes forecast, Climatology and forecast with
Climatology fallback. Loading a file preserves the selected policy. The Calculate
button remains visible while the setup scrolls. Completed passages show arrival,
duration, distance, speed, weather extrema and actual wind-source use, including
combined results for linked legs. Calculate explicitly recalculates the passage.

New passage retains the current boat and operating preferences while clearing linked-route
and calculation identity. Its weather source uses the last explicitly selected policy,
or Forecast with Climatology fallback before any explicit choice. Passage actions offers a separate example passage and
a previewed, scoped restore of operating preferences. Existing OpenCPN routes
can be selected directly; their ordered waypoints stay owned by OpenCPN.
Reordering, adding or removing waypoints requires a fresh linked sequence.
Weather requests cover all route waypoints. Context help and keyboard navigation
are available without choosing a learning mode.

Routes & analysis retains the existing lists, engines, reports, exports and
specialist tools. Right-click route analysis, generated legs and chart-position
actions remain available. A passage's generated legs use the existing sequential
calculation path. Departure and target-arrival planning use the same engines.

The example 40 ft monohull is illustrative. The searchable bundled library and
existing polar editor support personal boats and multiple sail configurations.
Boat imports copy their polars into version 2 storage and preserve the originals.
The library also lists saved boats and provides duplication and portable export
of XML with its polar files. Selecting a library polar creates a personal copy.
Optional boat/polar names, descriptions, source, sail configuration and notes
use backwards-compatible boat XML attributes. Missing provenance stays unknown;
metadata is not certification, and no new polar-table format is required.
Version 2 uses `/PlugIns/xWeatherRouting2` preferences and a separate
`plugins/xweather_routing_v2` data directory. Existing version 1 data are not
automatically changed or migrated.

The internal GRIB reader owns its forecast and supplies immutable frames to
the existing routing cache. It records per-variable time extents and a SHA256
identity. A failed replacement keeps the previous forecast; changing weather
during calculation is rejected. Forecast data remain selected when dialogs hide.
The xGRIB generator and its desktop dialog are integrated, including the existing
weather, wave, current, extension and merge capabilities. A passage prefills
the request area, UTC time, preliminary duration and required wave/current flags.
The request envelope is an estimate; actual masks and later coverage are checked
by routing and final validation.

The complete Climatology 2026.2 pack is bundled with its original provenance
and checksums. The routing reader loads neighbouring months on demand, preserves
missing masks, and exposes wind distributions, currents and historical cyclone
crossings without constructing a separate plugin's UI. A compatible installed
provider advertising a newer dataset takes precedence. Providers are stopped
and rebound safely on replacement or unload. Forecast-to-Climatology fallback
is an explicit planning choice; reports retain actual source use. Unknown waves
remain unknown. Fresh example operating limits are 30 kn true wind, 40 kn apparent
wind and 3 m significant waves, with currents and motoring off and land detection
on. These are visible starting settings to review for the vessel and passage.

## Local development and validation

Build out of tree and install into a private staging prefix. The minimum CMake
version is 3.20. The generator uses the dependencies documented in its retained
README; Jasper is a checksum-pinned static dependency. All Climatology outputs
are checked against the dataset manifest during configuration.

`tools/launch-isolated.py --host PATH --prefix PATH --profile NEW_DIRECTORY`
starts a private portable test instance. It refuses unmanaged existing profiles,
sets private XDG directories, and installs only the staged version 2 plugin.
It neither changes a working launcher nor installs into system/user plugin paths.

For Windows x64, see [source build and isolated test instructions](WINDOWS.md).
The helper builds separately from the matched OpenCPN/wx SDK and carries its
own scientific dependencies. Native CI checks both pinned wx 3.3.4 and 3.2.9
hosts; qualification evidence is recorded in [VALIDATION-2.2.md](VALIDATION-2.2.md).
macOS, Android's in-process generator adapter and the touch workspace require
their own follow-up qualification. The inherited 1.x matrix is not evidence
that these new paths have passed. Development packages do not replace the
stable catalogue release.
