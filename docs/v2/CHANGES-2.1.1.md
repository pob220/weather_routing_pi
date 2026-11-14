# xWeatherRouting 2.1.1

- Weather download requests for Copernicus NWS currents, including Auto requests
  which can select NWS, start at the preceding whole UTC hour. This also applies
  after manually editing the download time. For example, 16:01 becomes 16:00
  for the download only.
- The duration expands outward to cover the original requested end and selected
  forecast/wave intervals. Estimate jobs and generation jobs use the same window.
  The generator shows the adjusted download controls and a short explanation.
- Passage departure and target arrival times stay exactly as entered. Existing
  passages, boats, polars and selected weather-source policies are retained.
- Rick's ready-to-run Windows trial and the isolated Linux test are versioned
  2.1.1. The stable 1.36 catalogue line is unchanged.

The earlier local weather follow-up is included: fresh setups default to Forecast
with Climatology fallback and generated weather stays in version 2 storage.

The NWS reader requires exact hourly timestamps. The existing generic Copernicus
ARCO reader already samples a published frame within half its source interval;
it does not need an additional blanket three-hour request restriction. Tidal,
local-file and other providers retain their existing timestamp handling.

No routing engines, headless scenario behavior, native GRIB providers or
scientific dependencies were changed. Real forecast-range and coverage failures
remain errors; rounding does not create unavailable weather data.
