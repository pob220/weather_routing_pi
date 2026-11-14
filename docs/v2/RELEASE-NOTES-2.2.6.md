# Weather Routing 2.2.6

GRIB Time now works after a cold restart using the forecast loaded by Weather Routing, even when xGRIB is absent or closed. A viewer timeline outside that forecast falls back to its first wind time. Loading and generation remain independent of other plugins; an enabled xGRIB 0.4.5 can display the same file.

Android adds integrated forecast loading and generation to the existing Plan / Routes / Results / Tools workspace, shared passage readiness checks with recovery actions, and touch controls for the new workflow. Forecast coverage and diagnostic timestamps use the existing Android UTC formatter. The generator uses a private in-process scientific/TLS runtime.

Automatic settings preserve manual values and selected engines. Existing routing algorithms are unchanged. This branch is for further testing; alpha publication is pending Rick’s testing.
