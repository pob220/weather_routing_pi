# Weather Routing 2.2.5

Loading passage weather or generating a GRIB with “Open generated GRIB after creation” checked now asks an enabled GRIB viewer to load the same file. xGRIB 0.4.5 also opens its viewer and displays the chart overlay when it has not been opened yet. An already-visible xGRIB viewer replaces the old forecast with the newly selected one.

Routing loads and validates the file before notifying the viewer. No xGRIB installation is required for generation or routing. Leaving the generation checkbox unchecked sends no display request. Saved display layer preferences are preserved by xGRIB. All 2.2.4 automatic/adaptive settings, GUI control locking, provider fixes and routing engines are retained.
