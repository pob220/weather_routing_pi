// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <wx/string.h>
#include "GribReader.h"
#include "GribRecordSet.h"

namespace weather_routing {

// Owns the reader for the selected forecast. Frames own copies/interpolations;
// closing a weather window cannot replace the forecast used by a calculation.
class PassageWeather {
public:
  bool Open(const wxString& path, wxString& error);
  std::unique_ptr<GribRecordSet> Frame(time_t time) const;
  bool HasWind(time_t time, double lat, double lon) const;
  bool HasData(int index, time_t time, double lat, double lon) const;
  // Cheap advisory access to already decoded source grids. No copied frame,
  // file read or host request. Values retain the GRIB field's native units.
  std::optional<double> Sample(int index, time_t time, double lat, double lon) const;
  bool Loaded() const { return bool(reader_); }
  wxString Summary() const;
  wxString Details() const;
  const wxString& Path() const { return path_; }
  const std::string& Identity() const { return identity_; }
  time_t FirstWindTime() const;
  time_t LastWindTime() const;
  std::pair<time_t, time_t> Extent(int index) const;

private:
  std::unique_ptr<GribReader> reader_;
  std::array<std::map<time_t, GribRecord*>, Idx_COUNT> fields_;
  wxString path_;
  std::string identity_;
};
}  // namespace weather_routing
