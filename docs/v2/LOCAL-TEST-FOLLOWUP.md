# 2.1.1 local testing follow-up

Weather source selection is now remembered separately from incidental settings
saved during other edits. Fresh or inherited configurations with no recorded
user selection start new passages with Forecast with Climatology fallback.
An explicit weather selection in the planner or settings is remembered across
new passages and restarts. Existing saved passages keep their own source settings;
the offline example does not change the user's preferred source.

The optional boundary setting is labelled **Use OpenCPN Draw exclusion areas**.
It refers to custom areas supplied by OpenCPN Draw, independently of shoreline
land avoidance and chart/depth checking. If the setting is enabled without its
provider, passage checks explain the missing prerequisite before calculation
and link to Passage settings. The check does not silently remove the constraint.
Normal routing does not require OpenCPN Draw.

These UI/preferences follow-ups to the published 2.1.1 Windows trial at
`d3b8818` are included in 2.1.2. The old review ZIP remains pinned to its qualified source.
Routing engines, native GRIB providers and headless scenario parsing are unchanged.

Local validation: 653 enabled CTest cases passed, with the existing live timing
comparison still disabled. Private UI checks reproduced the old inherited
policy, verified the fallback before an explicit selection, remembered Forecast
only across new passages and restart, and retained a later explicit fallback.
Changing the boundary option left the weather preference and land avoidance
unchanged. The missing-provider recovery opens the correctly labelled setting.
