# Weather feedback fixes — 8 November 2026

This follow-up is installed in the local **xWeatherRouting 2.1.0 Test** staging (the earlier development build)
area. Restart that test copy to load it. Existing saved passages, vessel data
and explicitly chosen source policies are retained.

- Fresh setups now default to **Forecast with Climatology fallback**. The
  optional offline example still explicitly selects Climatology. Actual source
  use continues to be reported by the existing routing code.
- The passage-to-generator prefill previously copied a 16:01 departure directly
  into an hourly Copernicus NWS request. The reader correctly rejected the
  non-matching timestamp. The UI now starts downloads at the preceding whole
  UTC hour and rounds the duration outward to cover the original passage window
  and selected weather/wave intervals. The passage departure is unchanged.
- That earlier build explained manually entered non-hourly NWS request times
  before launching a download. [Version 2.1.1](CHANGES-2.1.1.md) now aligns them
  automatically, including the duration needed to retain the requested end. Real forecast-range failures remain errors and offer a plain
  explanation to review the start, duration or current source.
- The generator's default output/cache folder is now under the version 2 user
  data directory, including the isolated profile in portable tests. The
  inherited default used the normal OpenCPN GRIB folder. Existing files in that
  folder are retained.
- **Check generator** accurately labels the installed-capability check; it
  does not claim to check live forecast coverage. Successful-generation text
  refers to Weather Routing's integrated reader.

On 8 November the public Copernicus STAC metadata advertised whole-hour NWS
current timestamps through 14 November 00:00 UTC. The reported 8 November
16:01 departure was within the date range but not on that time axis. No account
passwords or current-field downloads were needed to establish the mismatch.

Validation: 650 enabled Linux CTest cases passed, with the existing live timing
comparison still disabled. Regression cases cover the reported minute offset,
seconds and milliseconds, midnight, long request windows, interval alignment
and invalid input. A fresh private UI profile displayed the new source default;
its actual generated request used whole-hour UTC times and its own output
folder. Routing-engine, headless and native generator/provider source files are
unchanged. An authenticated Copernicus generation was not rerun.

The original public 2.1 Windows trial remains pinned to `c4e363e`. These later
changes require their own native Windows qualification before a new review
bundle is published.
