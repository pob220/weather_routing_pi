// SPDX-License-Identifier: GPL-3.0-or-later
#include "PassageWeather.h"
#include "ShorelineDataset.h"
#ifdef __OCPN__ANDROID__
#include "TimeZoneDisplay.h"
#endif
#include <algorithm>
#include <cmath>
#include <wx/datetime.h>
#include <wx/filename.h>

namespace weather_routing {
namespace {
int Index(const GribRecord& r) {
  const int t = r.getDataType(), level = r.getLevelType(),
            value = r.getLevelValue();
  // Never mistake upper-air wind for the boat's surface wind.
  const bool surface =
      level == LV_GND_SURF || (level == LV_ABOV_GND && value == 10);
  if (surface && t == GRB_WIND_VX) return Idx_WIND_VX;
  if (surface && t == GRB_WIND_VY) return Idx_WIND_VY;
  if (surface && t == GRB_WIND_GUST) return Idx_WIND_GUST;
  if (level == LV_ISOBARIC) return -1;
  switch (t) {
    case GRB_UOGRD:
      return Idx_SEACURRENT_VX;
    case GRB_VOGRD:
      return Idx_SEACURRENT_VY;
    case GRB_HTSGW:
      return Idx_HTSIGW;
    case GRB_WVDIR:
    case GRB_DIR:
      return Idx_WVDIR;
    case GRB_WVPER:
    case GRB_PER:
      return Idx_WVPER;
    case GRB_PRESSURE:
      return Idx_PRESSURE;
    case GRB_TEMP:
      return Idx_AIR_TEMP;
    case GRB_WTMP:
      return Idx_SEA_TEMP;
    case GRB_CLOUD_TOT:
      return Idx_CLOUD_TOT;
    case GRB_PRECIP_TOT:
      return Idx_PRECIP_TOT;
    case GRB_HUMID_REL:
      return Idx_HUMID_RE;
    case GRB_CAPE:
      return Idx_CAPE;
    default:
      return -1;
  }
}
wxString UTC(time_t t) {
#ifdef __OCPN__ANDROID__
  return marine_time::FormatInTimeZone(wxDateTime(t), "%d %b %Y, %H:%M", "UTC");
#else
  return wxDateTime(t).Format("%d %b %Y, %H:%M", wxDateTime::UTC) + " UTC";
#endif
}
}  // namespace

bool PassageWeather::Open(const wxString& path, wxString& error) {
  try {
    auto next = std::make_unique<GribReader>(path);
    if (!next->isOk()) {
      error = next->isResourceLimitExceeded()
                  ? _("This GRIB exceeds the weather reader's memory limit. "
                      "Select a smaller area or coarser forecast.")
                  : _("This file contains no readable GRIB data. The previous "
                      "forecast has been kept.");
      return false;
    }
    decltype(fields_) fields;
    for (const auto& entry : *next->getGribMap())
      for (auto* record : *entry.second) {
        const int i = Index(*record);
        if (i >= 0 && record->isOk())
          fields[i][record->getRecordCurrentDate()] = record;
      }
    if (fields[Idx_WIND_VX].empty() || fields[Idx_WIND_VY].empty()) {
      error =
          _("This GRIB has no complete surface-wind vector. Choose a forecast "
            "with 10 m wind. The previous forecast has been kept.");
      return false;
    }
    reader_ = std::move(next);
    fields_ = std::move(fields);
    path_ = path;
    identity_ = "grib-sha256:" +
                ShorelineSha256(std::filesystem::u8path(path.ToUTF8().data()));
    error.clear();
    return true;
  } catch (const std::exception& e) {
    error = _("Unable to load this GRIB: ") + wxString::FromUTF8(e.what());
    return false;
  }
}

std::unique_ptr<GribRecordSet> PassageWeather::Frame(time_t time) const {
  auto frame = std::make_unique<GribRecordSet>(0);
  frame->m_Reference_Time = time;
  for (int i = 0; i < Idx_COUNT; ++i) {
    const auto& series = fields_[i];
    auto after = series.lower_bound(time);
    if (after == series.end()) continue;
    if (after->first == time) {
      frame->SetUnRefGribRecord(i, new GribRecord(*after->second));
    } else if (after != series.begin()) {
      const auto before = std::prev(after);
      const double weight =
          double(time - before->first) / (after->first - before->first);
      frame->SetUnRefGribRecord(
          i, GribRecord::InterpolatedRecord(*before->second, *after->second,
                                            weight, i == Idx_WVDIR));
    }
  }
  return frame;
}

std::optional<double> PassageWeather::Sample(
    int index, time_t time, double lat, double lon) const {
  if (index < 0 || index >= Idx_COUNT || !std::isfinite(lat) || !std::isfinite(lon))
    return {};
  const auto& series = fields_[index];
  const auto after = series.lower_bound(time);
  if (after == series.end()) return {};
  const auto sample = [&](const GribRecord* record) -> std::optional<double> {
    const double value = record->getInterpolatedValue(lon, lat);
    return std::isfinite(value) && value != GRIB_NOTDEF
        ? std::optional<double>(value) : std::nullopt;
  };
  const auto a = sample(after->second);
  if (after->first == time || !a) return a;
  if (after == series.begin()) return {};
  const auto before = std::prev(after);
  // Different grids need the normal frame/regridding path. Sparse advisory
  // probes must not invent values across a resolution or footprint change.
  const auto* x = before->second;
  const auto* y = after->second;
  if (x->getNi() != y->getNi() || x->getNj() != y->getNj() ||
      x->getLonMin() != y->getLonMin() || x->getLonMax() != y->getLonMax() ||
      x->getLatMin() != y->getLatMin() || x->getLatMax() != y->getLatMax()) return {};
  const auto b = sample(x);
  if (!b) return {};
  const double weight = double(time - before->first) / (after->first - before->first);
  if (index == Idx_WVDIR) {
    double delta = std::remainder(*a - *b, 360.);
    return std::fmod(*b + weight * delta + 360., 360.);
  }
  return *b + weight * (*a - *b);
}

bool PassageWeather::HasWind(time_t time, double lat, double lon) const {
  return HasData(Idx_WIND_VX, time, lat, lon) &&
         HasData(Idx_WIND_VY, time, lat, lon);
}
bool PassageWeather::HasData(int index, time_t time, double lat, double lon) const {
    const auto& s = fields_.at(index);
    auto after = s.lower_bound(time);
    if (after == s.end()) return false;
    const double a = after->second->getInterpolatedValue(lon, lat);
    if (!std::isfinite(a) || a == GRIB_NOTDEF) return false;
    if (after->first != time) {
      if (after == s.begin()) return false;
      const double b = std::prev(after)->second->getInterpolatedValue(lon, lat);
      if (!std::isfinite(b) || b == GRIB_NOTDEF) return false;
    }
  return true;
}

std::pair<time_t, time_t> PassageWeather::Extent(int index) const {
  const auto& s = fields_.at(index);
  return s.empty() ? std::make_pair(time_t(-1), time_t(-1))
                   : std::make_pair(s.begin()->first, s.rbegin()->first);
}
time_t PassageWeather::FirstWindTime() const {
  return std::max(Extent(Idx_WIND_VX).first, Extent(Idx_WIND_VY).first);
}
time_t PassageWeather::LastWindTime() const {
  return std::min(Extent(Idx_WIND_VX).second, Extent(Idx_WIND_VY).second);
}
wxString PassageWeather::Summary() const {
  return Loaded() ? wxFileName(path_).GetFullName() : _("No forecast selected");
}
wxString PassageWeather::Details() const {
  if (!Loaded())
    return _("Load a GRIB, download weather, or select Climatology.");
  wxString result =
      _("Wind: ") + UTC(FirstWindTime()) + " - " + UTC(LastWindTime());
  for (const auto& item : {std::make_pair(Idx_HTSIGW, _("Waves")),
                           std::make_pair(Idx_SEACURRENT_VX, _("Currents"))}) {
    auto e = Extent(item.first);
    result += "\n" + item.second + ": " +
              (e.first < 0 ? _("not available")
                           : UTC(e.first) + " - " + UTC(e.second));
  }
  return result;
}
}  // namespace weather_routing
