# xWeatherRouting 2.1.1 qualification

This patch changes weather-request preparation and the versioned review bundle.
Routing engines, headless scenarios and native GRIB provider code are unchanged.
The previous [2.1.0 qualification](VALIDATION-2.1.0.md) is retained as history.

The [published 2.1.1 review release](https://github.com/pob220/xweather_routing_pi/releases/tag/windows-ui-trial-2.1.1)
records the exact qualified source, native Windows run, ZIP checksum and test
receipts. Qualification includes both pinned Windows x64 hosts (wxWidgets 3.3.4
and 3.2.9), their test suites and packaged-helper checks, and the actual portable
launcher's behavior after relocation. The unchanged scientific helper is reused
only after its source and build inputs match the qualified original.

Linux checks cover automatic NWS/Auto request alignment, outward duration
rounding, timezone conversion, and preservation of passage times. The isolated
local test installation uses the existing version 2 profile and its own data;
the working OpenCPN and Weather Routing 1.36 are separate.

Authenticated Copernicus downloads are not claimed by these offline tests.
Provider date-range and geographic coverage checks retain their existing behavior.
