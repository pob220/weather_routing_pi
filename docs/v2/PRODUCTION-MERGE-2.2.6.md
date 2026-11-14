# Production-based Weather Routing 2.2.6 branch

This development successor is based on the current production 1.36.3 candidate
e93f6bd40b2ea4e7c334610b098b1d7f9923f107 and merges the Android-qualified
xWeatherRouting 2.2.6 source 475aaadb39b7ab4558e965587734af83eb0dc91a.
It does not replace or republish the stable 1.36.3 release.

The canonical build defaults to the WeatherRouting identity. The fork identity
remains explicit and automatic in xweather_routing_pi CI. Android uses the
selected product name, and the canonical Windows workflow does not run the
xWeather-specific portable launcher job.

Production's newer opencpn-libs pin 8544820 is retained, including its SDK and
JSON updates. Version 2's new-passage preference layer provides Auto with every
engine selected by default and retains explicit saved choices. Its historical
engine settings reader stays unchanged. All 175 frozen engine/model inputs match
the tested 2.x source; the freeze manifest and guard are unchanged.

Local qualification on 14 November 2026: full Release plugin/helper build,
715 enabled tests passed, one existing timing comparison disabled. An isolated
canonical-identity OpenCPN GUI test passed restored-forecast GRIB Time with an
absent/out-of-range viewer clock and preserved an in-range clock; the saved epoch
matched. The Android hardware record is in QUALIFICATION-2.2.6.md and covers the
xWeather identity. Further platform qualification and Rick's testing precede any
alpha or catalogue publication.

The canonical arm64 Android library and import archive also build against the
pinned stock 5.14 host. TLS isolation, 16 KiB ELF alignment, import metadata and
shoreline-package checks pass. All 1,013 wx imports resolve against both that
stock host and the actual tablet's 5.15 host. This is build/ABI qualification;
the hardware workflow was exercised with the xWeather identity, not by replacing
the tablet's navigation profile with the canonical variant.
