# 2.2.6 desktop and Android qualification

14 November 2026. Development testing; not an alpha or catalogue release.
Routing kernels and the numerical adapter are unchanged by this pass.

## Desktop

All 715 enabled Linux tests passed; the existing ocean timing comparison remains
disabled. An isolated OpenCPN 5.15 GUI test restored a passage forecast without
xGRIB installed. GRIB Time selected its first wind timestamp when the external
clock was absent or outside coverage, and preserved an in-range external choice.
The saved configuration's epoch matched the displayed date and time.

This own-forecast cold-start fix is specific to version 2. Production 1.36.3
uses an external GRIB provider and does not have that restoration path.

## Physical Android tablet

Samsung SM-X210, Android 15, arm64, OpenCPN development host 5.15.0 chart-aware,
plugin API 1.21, wxQt 3.1 / Qt 5.12.2. Tests used an owned routing profile and
position pair, without adding OpenCPN navigation objects.

- The Plan / Routes / Results / Tools workspace loads the resident forecast,
  reports readiness failures and exposes recovery actions. Missing forecast
  recovery opens the touch file picker.
- A 7.16 nm real-GRIB route completed with Automatic settings and Quick, using
  4,449 seconds of GRIB wind and no Climatology wind.
- Live NOAA GFS generation ran in process, producing 36 messages, approximately
  38 KiB, with wind coverage 14 November 12:00 UTC to 15 November 12:00 UTC.
  An independent ecCodes inspection confirmed the output. An enabled xGRIB 0.4.5
  displayed the same file after the generator closed.
- With xGRIB disabled and after a cold start, generation, cancellation and
  restart worked. A 25.06 nm route completed with 12,219 seconds of GRIB wind
  and no Climatology wind. Its GPX has 15 positions, the expected endpoints,
  and UTC times 13:00:00 to 16:23:39.
- xGRIB timeline movement showed the interpolated route marker at 15:00, and
  no route marker at 12:00 before departure or 18:00 after arrival.
- All six engines were selected for Auto and All. Automatic settings disabled
  their numerical inputs. Switching to manual allowed a typed 7.5-degree
  heading interval; saving, reopening and re-enabling Automatic retained it.
- The GRIB Time button worked with the viewer unopened. Native calendar and
  hour/minute controls, rotation, scrolling and layered Android Back passed.
- File-picker swipes scroll without opening a folder. The filename, heading and
  primary actions remain above the landscape keyboard. Cancel works while
  typing. Progress timing and touch tabs remain visible.
- At approximately 731 MiB available RAM, the 128 MiB requested forecast cache
  was capped to 91 MiB. Forecast grid resolution was preserved.

Private backups and hash checks cover the original tablet profile. All 751
original routing files and all four stable Weather Routing preference sections
were unchanged. Navigation table counts and SQLite integrity passed. xGRIB's
pre-test preferences were restored. The working laptop OpenCPN was not changed.

## Package and limits

The Android archive includes import metadata, the CA bundle, dependency
licenses, bundled Climatology and compact shoreline assets. TLS symbol isolation,
16 KiB ELF alignment and shoreline-package checks passed. New wx imports resolve
against the actual 5.15 host and the pinned stock 5.14 host.

This tablet has a 4 KiB kernel; ELF alignment does not establish a runtime test
on a 16 KiB Android device. The stock 5.14 import check is an ABI check, not a
second physical-device test. Copernicus authentication and authenticated live
products were not exercised. The direct shared-waypoint editor's save path was
reviewed but was not used to mutate the user's navigation database.

The in-process generator and secure-storage adapter derive from qualified
xGRIB 0.4.5 source a5606ce, with Weather Routing storage and credential scope.
Windows qualification is recorded separately when the native build finishes.
