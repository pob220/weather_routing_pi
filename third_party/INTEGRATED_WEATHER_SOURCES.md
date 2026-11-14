# Integrated weather sources

The GRIB reader, generator dialog and supporting UI are adapted from xGRIB
0.4.3, commit a9744a1. The environmental-grib-generator source snapshot is
copied from the locally pinned xGRIB generator submodule, commit
3922855f4d4709c7d14187d01314c1d3ee82bfd7; its original LICENSE
is retained. Climatology wind/current interpolation is adapted from the
Climatology plugin and retains its GPL-3.0-or-later attribution. The bundled
2026.2 data include the original manifest and dataset provenance.

The routing plugin owns its weather reader and its user settings. No separate
GRIB, xGRIB, Polars or Climatology installation is required.
