# xWeatherRouting 2.1.2

This patch addresses the Windows display problems in Rick's screenshots.

- Bullets, arrows, dashes, ellipses and other non-ASCII UI punctuation use
  explicit Unicode strings rather than Windows' default ANSI conversion.
  Existing translations keep the same message text.
- Linked passages whose OpenCPN route has no name show the whole passage's
  start and destination, with the leg number, instead of the internal route ID.
  Named routes retain their names. Stored identifiers and route linkage are unchanged.
- New passages use Forecast with Climatology fallback until the user explicitly
  chooses another weather policy. The explicit choice is remembered across
  new passages and restarts. Existing saved passages retain their own policy.
- The optional exclusion-area setting clearly names OpenCPN Draw. Missing
  boundary data is explained before calculation, with a link to the setting;
  shoreline land avoidance remains a separate control.

Routing engines, headless scenario parsing, boat/polar data and scientific
weather providers are unchanged by this patch. The 2.1.1 hourly Copernicus
request fix is retained without changing passage departure or arrival times.

Regression checks include Unicode passage labels under a Windows ANSI locale,
unnamed/named passage labels and inspection of the actual running Windows
planner labels through the delivered portable launcher. Release qualification
is recorded in [VALIDATION.md](VALIDATION.md).
