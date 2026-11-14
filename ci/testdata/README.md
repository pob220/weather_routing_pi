# Windows host acceptance forecast

`irish-sea-uniform-wind.grb` is a synthetic GRIB1 fixture created with ECMWF
ecCodes `grib_set` and its `regular_ll_sfc_grib1.tmpl` sample. It contains 10 m
U/V wind fields on a 41 by 31 regular latitude/longitude grid: 57 to 51 N,
9 to 1 W, 0.2 degree spacing. U is 6 m/s and V is zero at every point.
The reference time is 2026-09-28 00:00 UTC; forecast steps are 0 through 24
hours in three-hour increments. It contains no operational forecast data.
It uses GRIB1 table 2 parameter codes 33/34, height type 105 and level 10,
which the bundled OpenCPN reader recognizes as surface U/V wind. The original
ECMWF sample's 131/132 parameter codes are not its 10 m wind encoding.

The host test uses a short offshore route, the packaged example polar, the
bundled GRIB plugin's public file-open interface, and Weather Routing's
existing headless scenario interface. It checks actual packaged-plugin
loading and route completion in the unmodified upstream Windows host.

`irish-sea-three-hour-wind.grb` retains only the first four messages of that
fixture (U/V at zero and three hours). The v2 host acceptance test uses it for
a longer route and requires reported Climatology wind use after forecast expiry.


## Farallon JPEG2000 decoder regression

`farallon-jpeg2000.grb2` is a lossless extraction of messages 5, 6, 13, 14,
598, 599 and 600 from the archived real forecast
`ocpn_ecmwf0p25_999_2026-10-14-17-48.grb2` (original SHA-256
`575b2a39c35171034d4067f154c5c5719fc8938f7ec91be2b2d348f6dae34a55`).
It contains 10 m U/V wind at 2026-10-13 12:00 and 15:00 UTC, plus wave height,
direction and period at 12:00. The seven 20 by 9 grids retain their original
JPEG2000 packing; each wave grid has 68 missing points in its bitmap.

Reproduce the extraction with:

```
grib_copy -w count=5/6/13/14/598/599/600 original.grb2 farallon-jpeg2000.grb2
```

`farallon-jpeg2000-values.txt` records every grid value, including missing
points, independently decoded with ECMWF ecCodes `grib_get_data`, using
`-m -999999999 -F %.12g -L '%12.6f%12.6f'`. Tests need no ecCodes runtime.
The fixture reproduced the integrated reader's uninitialized-JasPer abort.

The regression also damages JPEG2000 marker data while leaving the GRIB and
SIZ geometry intact. It verifies clean decoder failure, preservation of an
already selected forecast, and a successful subsequent valid load.
