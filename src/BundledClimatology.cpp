// SPDX-License-Identifier: GPL-3.0-or-later
// Wind/current interpolation adapted from ClimatologyOverlayFactory,
// Copyright (C) 2014 Sean D'Epagnier and OpenCPN contributors.
#include "BundledClimatology.h"
#include "ClimatologyMath.h"
#include "zuFile.h"
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <json/json.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace weather_routing {
namespace {
constexpr double kPi = 3.14159265358979323846;
double positive_degrees(double v) { return climatology::Wrap360(v); }
double interp_value(double a, double b, double w) {
  return climatology::InterpolateMissing(a, b, w);
}
double interp_angle(double a, double b, double w) {
  return climatology::InterpolateAngleRadians(a, b, w);
}
enum Coord { U, V, MAG, DIRECTION };

struct WindData {
  struct WindPolar {
    WindPolar() {}
    ~WindPolar() { /*delete [] directions; delete [] speeds;*/ }
    wxUint8 gale, calm, directions[8] /*{}*/, speeds[8] /*{}*/;
    double Value(enum Coord coord, int dir_cnt);
  };

  WindData(int lats, int lons, int dirs, float dir_res, float spd_mul)
      : latitudes(lats),
        longitudes(lons),
        dir_cnt(dirs),
        direction_resolution(dir_res),
        speed_multiplier(spd_mul),
        data(NULL) {
    if (latitudes <= 0 || longitudes <= 0 || dir_cnt != 8 ||
        direction_resolution <= 0 || speed_multiplier <= 0)
      throw std::bad_alloc();
    const size_t cells = static_cast<size_t>(latitudes) * longitudes;
    if (cells > SIZE_MAX / sizeof(WindPolar)) throw std::bad_alloc();
    data = new WindPolar[cells]();
  }
  ~WindData() { delete[] data; }

  double InterpWind(enum Coord coord, double lat, double lon);
  WindPolar* GetPolar(double lat, double lon) {
    if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90 || lat > 90)
      return NULL;
    int lati = static_cast<int>(floor(latitudes * (lat + 90.0) / 180.0));
    if (lati == latitudes) lati = latitudes - 1;
    lon = positive_degrees(lon);
    int loni = static_cast<int>(floor(longitudes * lon / 360.0));

    WindPolar* polar = &data[lati * longitudes + loni];
    if (polar->gale == 255) return NULL;

    return polar;
  }

  int latitudes, longitudes, dir_cnt;
  float direction_resolution, speed_multiplier;
  WindPolar* data;
};

struct CurrentData {
  CurrentData(int lats, int lons, int mul)
      : latitudes(lats), longitudes(lons), multiplier(mul) {
    if (latitudes <= 0 || longitudes <= 0) throw std::bad_alloc();
    size_t elems =
        static_cast<size_t>(latitudes) * static_cast<size_t>(longitudes);
    if (elems == 0 || elems > (SIZE_MAX / sizeof(float)))
      throw std::bad_alloc();
    data[0].assign(elems, 0.0f);
    data[1].assign(elems, 0.0f);
  }

  double Value(enum Coord coord, int xi, int yi);
  double InterpCurrent(enum Coord coord, double lat, double lon);

  int latitudes, longitudes, multiplier;
  std::vector<float> data[2];
};

double WindData::WindPolar::Value(enum Coord coord, int dir_cnt) {
  if (gale == 255) return NAN;

  if (coord == DIRECTION)  // maybe should do most likely here rather than
                           // vector average?
    return atan2(Value(U, dir_cnt), Value(V, dir_cnt));

  int totald = 0, totals = 0;
  for (int i = 0; i < dir_cnt; i++) {
    totald += directions[i];

    double mul = 0;
    switch (coord) {
      case U:
        mul = sin(i * 2 * kPi / dir_cnt);
        break;
      case V:
        mul = cos(i * 2 * kPi / dir_cnt);
        break;
      case MAG:
        mul = 1;
        break;
      default:
        printf("error, invalid coord: %d\n", coord);
    }

    totals += mul * speeds[i] * directions[i];
  }
  return totald ? (double)totals / totald : NAN;
}

double CurrentData::Value(enum Coord coord, int xi, int yi) {
  if (xi < 0 || xi >= latitudes || yi < 0 || yi >= longitudes) return NAN;

  double u = data[0][xi * longitudes + yi], v = data[1][xi * longitudes + yi];
  switch (coord) {
    case U:
      return u;
    case V:
      return v;
    case MAG:
      return hypot(u, v);
    case DIRECTION:
      return !u && !v ? NAN : atan2(u, v);
    default:
      printf("error, invalid coord: %d\n", coord);
  }
  return NAN;
}

double WindData::InterpWind(enum Coord coord, double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < -90.0 || x > 90.0)
    return NAN;
  double latoff = 90.0 / latitudes, lonoff = 180.0 / longitudes;

  double xi = latitudes * (.5 + (x - latoff) / 180.0);
  double yi = longitudes * positive_degrees(y - lonoff) / 360.0;

  int h = longitudes, d = dir_cnt;

  if (yi < 0) yi += h;

  if (xi < 0) xi = 0;
  if (xi > latitudes - 1) xi = latitudes - 1;
  int x0 = floor(xi), x1 = x0 + 1;
  int y0 = floor(yi), y1 = y0 + 1;
  int y1v = y1;
  if (x1 == latitudes) x1 = x0;
  if (y1v == h) y1v = 0;

  double v00 = data[x0 * h + y0].Value(coord, d),
         v01 = data[x0 * h + y1v].Value(coord, d);
  double v10 = data[x1 * h + y0].Value(coord, d),
         v11 = data[x1 * h + y1v].Value(coord, d);

  if (coord == DIRECTION) {
    double a0 = interp_angle(v00, v01, yi - y0);
    double a1 = interp_angle(v10, v11, yi - y0);
    return interp_angle(a0, a1, xi - x0) * 180 / kPi;
  }

  double v0 = interp_value(v00, v01, yi - y0);
  double v1 = interp_value(v10, v11, yi - y0);
  return interp_value(v0, v1, xi - x0) / speed_multiplier;
}

double CurrentData::InterpCurrent(enum Coord coord, double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < -80.0 || x > 80.0)
    return NAN;
  y = positive_degrees(y);
  if (coord == MAG || coord == DIRECTION) {
    const double u = InterpCurrent(U, x, y);
    const double v = InterpCurrent(V, x, y);
    if (std::isnan(u) || std::isnan(v)) return NAN;
    if (coord == MAG) return hypot(u, v);
    return (!u && !v) ? NAN : atan2(u, v) * 180 / kPi;
  }
  double xi = (latitudes - 1) * (.5 - x / 160.0);
  double yi = longitudes * y / 360.0;
  int h = longitudes;

  int x0 = floor(xi), x1 = x0 + 1;
  int y0 = floor(yi), y1 = y0 + 1;
  int y1v = y1;
  if (x1 == latitudes) x1 = x0;
  if (y1v == h) y1v = 0;

  double v00 = Value(coord, x0, y0), v01 = Value(coord, x0, y1v);
  double v10 = Value(coord, x1, y0), v11 = Value(coord, x1, y1v);

  double v0 = interp_value(v00, v01, yi - y0);
  double v1 = interp_value(v10, v11, yi - y0);
  return interp_value(v0, v1, xi - x0);
}

wxString directory, source;
std::recursive_mutex mutex;
struct Month {
  std::unique_ptr<WindData> wind;
  std::unique_ptr<CurrentData> current;
};
std::map<int, std::shared_ptr<Month>> months;
std::list<int> monthRecency;
struct TrackSegment {
  double lat1, lon1, lat2, lon2;
  int day;
};
std::map<std::pair<int, int>, std::vector<TrackSegment>> tracks;
bool tracksLoaded = false;
struct Input {
  ZUFILE* file;
  explicit Input(const wxString& path)
      : file(zu_open_wx(path, "rb", ZU_COMPRESS_AUTO)) {
    if (!file) throw std::runtime_error("missing climatology file");
  }
  ~Input() { zu_close(file); }
  unsigned Byte() {
    unsigned char b;
    if (zu_read(file, &b, 1) != 1)
      throw std::runtime_error("truncated climatology file");
    return b;
  }
  unsigned Word() {
    unsigned lo = Byte();
    return lo | (Byte() << 8);
  }
};
std::shared_ptr<Month> Load(int month) {
  auto found = months.find(month);
  if (found != months.end()) {
    monthRecency.remove(month);
    monthRecency.push_back(month);
    return found->second;
  }
  auto result = std::make_shared<Month>();
  Input in(directory + wxString::Format("/wind%02d.gz", month + 1));
  unsigned h[7];
  for (auto& v : h) v = in.Word();
  if (h[0] != 0xfefe || !h[1] || h[1] > 2880 || !h[2] || h[2] > 5760 ||
      h[3] != 8 || !h[4] || !h[5] || !h[6])
    throw std::runtime_error("invalid wind header");
  auto& wind = result->wind;
  wind.reset(new WindData(h[1], h[2], 8, h[4], double(h[5]) / h[6]));
  const size_t cells = size_t(h[1]) * h[2];
  for (int pass = 0; pass < 17; ++pass)
    for (size_t i = 0; i < cells; ++i) {
      auto& p = wind->data[i];
      if (!pass) {
        const unsigned b = in.Byte();
        p.gale = b > 200 ? 255 : b >= 100 ? b - 100 : 0;
        p.calm = b < 100 ? b : 0;
      } else if (p.gale != 255) {
        if (pass <= 8)
          p.directions[pass - 1] = in.Byte();
        else
          p.speeds[pass - 9] = p.directions[pass - 9] ? in.Byte() : 0;
      }
    }
  Input extras(directory + wxString::Format("/wind-extras%02d.gz", month + 1));
  if (extras.Byte() != 'W' || extras.Byte() != 'E' || extras.Byte() != 'X' ||
      extras.Byte() != '1' || extras.Word() != h[1] || extras.Word() != h[2])
    throw std::runtime_error("invalid wind extras");
  std::vector<unsigned char> calm(cells), gale(cells);
  for (auto& b : calm) b = extras.Byte();
  for (auto& b : gale) b = extras.Byte();
  for (size_t i = 0; i < cells; ++i) {
    auto& p = wind->data[i];
    if (p.gale == 255) {
      if (calm[i] != 255 || gale[i] != 255)
        throw std::runtime_error("inconsistent wind mask");
    } else {
      if (calm[i] > 100 || gale[i] > 100)
        throw std::runtime_error("invalid wind probability");
      p.calm = calm[i];
      p.gale = gale[i];
    }
  }
  // Currents are independently optional: missing current must not suppress
  // wind.
  try {
    Input current(directory + wxString::Format("/current%02d.gz", month + 1));
    const unsigned rows = current.Word(), columns = current.Word(),
                   multiplier = current.Word();
    if (!rows || rows > 4096 || !columns || columns > 8192 || !multiplier)
      throw std::runtime_error("invalid current header");
    result->current.reset(new CurrentData(rows, columns, multiplier));
    for (auto& plane : result->current->data)
      for (auto& v : plane) {
        const int b = static_cast<int8_t>(current.Byte());
        v = b == -128 ? NAN : double(b) / multiplier;
      }
  } catch (...) {
    result->current.reset();
  }
  // Shared handles keep the two months alive while admitting a neighbouring
  // month.
  if (months.size() >= 4) {
    months.erase(monthRecency.front());
    monthRecency.pop_front();
  }
  months[month] = result;
  monthRecency.push_back(month);
  return result;
}
climatology::MonthInterpolationResult Interpolation(const wxDateTime& date) {
  const auto utc = date.GetTm(wxDateTime::UTC);
  return climatology::MonthInterpolation(
      utc.mon, utc.mday, (utc.hour + utc.min / 60.0) / 24.0,
      wxDateTime::GetNumberOfDays(utc.mon, utc.year));
}
std::pair<int, int> Dataset(const std::string& s) {
  int year = 0, revision = 0;
  if (sscanf(s.c_str(), "ocpn-climatology-%d.%d", &year, &revision) != 2)
    return {};
  return {year, revision};
}
}  // namespace

void BundledClimatology::Configure(const wxString& path) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  directory = path;
  months.clear();
  monthRecency.clear();
  tracks.clear();
  tracksLoaded = false;
  source = "Bundled Climatology 2026.2";
}
ClimatologyMessage BundledClimatology::Resolve(
    const ClimatologyMessage& external) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (external.valid && external.supported && external.data &&
      external.wind_atlas &&
      Dataset(external.dataset) > std::make_pair(2026, 2)) {
    source = "Installed Climatology: " + wxString::FromUTF8(external.dataset.c_str());
    return external;
  }
  source = "Bundled Climatology 2026.2";
  ClimatologyMessage result;
  result.valid = result.supported = true;
  result.major = 2;
  result.minor = 0;
  result.dataset = "ocpn-climatology-2026.2";
  result.data = Data;
  result.wind_atlas = Atlas;
  result.cyclones = Cyclones;
  return result;
}
wxString BundledClimatology::Source() {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  return source;
}
bool BundledClimatology::Atlas(const wxDateTime& date, double lat, double lon,
                               int& count, double* frequencies, double* speeds,
                               double& gale, double& calm) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!date.IsValid() || count != 8 || !std::isfinite(lat) ||
      !std::isfinite(lon))
    return false;
  try {
    const auto t = Interpolation(date);
    auto a = Load(t.month), b = Load(t.neighbour);
    auto p = a->wind->GetPolar(lat, lon), q = b->wind->GetPolar(lat, lon);
    if (!p || !q) return false;
    const double w = t.current_weight;
    gale = (w * p->gale + (1 - w) * q->gale) / 100.0;
    calm = (w * p->calm + (1 - w) * q->calm) / 100.0;
    for (int i = 0; i < 8; ++i) {
      const double f = p->directions[i] / a->wind->direction_resolution;
      const double g = q->directions[i] / b->wind->direction_resolution;
      frequencies[i] = w * f + (1 - w) * g;
      const double u = p->speeds[i] / a->wind->speed_multiplier;
      const double v = q->speeds[i] / b->wind->speed_multiplier;
      speeds[i] = f ? (g ? w * u + (1 - w) * v : u) : (g ? v : 0);
    }
    return true;
  } catch (...) {
    return false;
  }
}
bool BundledClimatology::Data(int setting, const wxDateTime& date, double lat,
                              double lon, double& direction, double& speed) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!date.IsValid() || !std::isfinite(lat) || !std::isfinite(lon) ||
      lat < -90 || lat > 90)
    return false;
  try {
    const auto t = Interpolation(date);
    auto a = Load(t.month), b = Load(t.neighbour);
    auto value = [&](Coord c) {
      const double x = setting == 0 ? a->wind->InterpWind(c, lat, lon)
                       : a->current ? a->current->InterpCurrent(c, lat, lon)
                                    : NAN;
      const double y = setting == 0 ? b->wind->InterpWind(c, lat, lon)
                       : b->current ? b->current->InterpCurrent(c, lat, lon)
                                    : NAN;
      return c == DIRECTION
                 ? climatology::InterpolateAngleRadians(
                       x * kPi / 180, y * kPi / 180, 1 - t.current_weight) *
                       180 / kPi
                 : climatology::InterpolateMissing(x, y, 1 - t.current_weight);
    };
    if (setting != 0 && setting != 1) return false;
    direction = value(DIRECTION);
    speed = value(MAG);
    if (direction < 0) direction += 360;
    return std::isfinite(direction) && std::isfinite(speed);
  } catch (...) {
    return false;
  }
}

int BundledClimatology::Cyclones(double lat1, double lon1, double lat2,
                                 double lon2, const wxDateTime& date,
                                 int days) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  if (!days) return 0;  // Capability probe, matching the legacy ABI.
  if (!date.IsValid() || !std::isfinite(lat1) || !std::isfinite(lat2) ||
      !std::isfinite(lon1) || !std::isfinite(lon2))
    return -1;
  auto align = [](double x, double reference) {
    while (x - reference > 180) x -= 360;
    while (x - reference < -180) x += 360;
    return x;
  };
  auto key = [](double latitude, double longitude) {
    return std::make_pair(int(std::floor(latitude)),
                          int(std::floor(climatology::Wrap360(longitude))));
  };
  try {
    if (!tracksLoaded) {
      decltype(tracks) next;
      for (const auto& theatre : {"atl", "epa", "nio", "she", "spa", "wpa"}) {
        const bool south =
            std::string(theatre) == "she" || std::string(theatre) == "spa";
        Input input(directory + "/cyclone-" + theatre + ".gz");
        for (;;) {
          unsigned char year[2];
          const int n = zu_read(input.file, year, 2);
          if (!n) break;
          if (n != 2) throw std::runtime_error("truncated cyclone year");
          const int y = year[0] + 256 * year[1];
          if (y < 1800 || y > 2200)
            throw std::runtime_error("invalid cyclone year");
          double lastLat = 0, lastLon = 0;
          int lastDay = 0;
          unsigned lastState = 0;
          bool previous = false;
          for (;;) {
            const unsigned state = input.Byte();
            if (state == 128) break;
            if (state != '*' && state != 'S' && state != 'E' && state != 'W' &&
                state != 'L' && state != 'D' && state != 'X')
              throw std::runtime_error("invalid cyclone state");
            const int packed = input.Byte(), month = input.Byte();
            if (packed / 4 < 1 || packed / 4 > 31 || month < 1 || month > 12)
              throw std::runtime_error("invalid cyclone time");
            const double a =
                static_cast<int16_t>(input.Word()) / 10.0 * (south ? -1 : 1);
            const double b = static_cast<int16_t>(input.Word()) / 10.0;
            input.Byte();
            input.Word();  // Wind and pressure are retained in source, not
                           // filters here.
            if (std::abs(a) > 90 || b > 15 || b < -360)
              throw std::runtime_error("invalid cyclone position");
            const double end = align(b, lastLon);
            // Default Climatology track classes: tropical, subtropical and
            // extratropical.
            if (previous &&
                (lastState == '*' || lastState == 'S' || lastState == 'E')) {
              TrackSegment seg{lastLat, lastLon, a, end, lastDay};
              for (int row = int(std::floor(std::min(lastLat, a)));
                   row <= int(std::floor(std::max(lastLat, a))); ++row)
                for (int col = int(std::floor(std::min(lastLon, end)));
                     col <= int(std::floor(std::max(lastLon, end))); ++col)
                  next[key(row, col)].push_back(seg);
            }
            previous = true;
            lastLat = a;
            lastLon = b;
            lastDay = (month - 1) * 365 / 12 + packed / 4 - 1;
            lastState = state;
          }
        }
      }
      tracks = std::move(next);
      tracksLoaded = true;
    }
    lon1 = climatology::Wrap360(lon1);
    lon2 = align(lon2, lon1);
    const auto utc = date.GetTm(wxDateTime::UTC);
    const int day = utc.mon * 365 / 12 + utc.mday - 1;
    const int range = std::min(days / 2, 182);
    auto cross = [](double ax, double ay, double bx, double by, double cx,
                    double cy) {
      return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    };
    for (int row = int(std::floor(std::min(lat1, lat2)));
         row <= int(std::floor(std::max(lat1, lat2))); ++row)
      for (int col = int(std::floor(std::min(lon1, lon2)));
           col <= int(std::floor(std::max(lon1, lon2))); ++col) {
        auto found = tracks.find(key(row, col));
        if (found == tracks.end()) continue;
        for (auto seg : found->second) {
          if (days < 365 &&
              climatology::CircularDayDistance(day, seg.day) > range)
            continue;
          const double shift = align(seg.lon1, lon1) - seg.lon1;
          seg.lon1 += shift;
          seg.lon2 += shift;
          const double a = cross(lat1, lon1, lat2, lon2, seg.lat1, seg.lon1);
          const double b = cross(lat1, lon1, lat2, lon2, seg.lat2, seg.lon2);
          const double c =
              cross(seg.lat1, seg.lon1, seg.lat2, seg.lon2, lat1, lon1);
          const double d =
              cross(seg.lat1, seg.lon1, seg.lat2, seg.lon2, lat2, lon2);
          if (a * b <= 0 && c * d <= 0 &&
              std::max(std::min(lat1, lat2), std::min(seg.lat1, seg.lat2)) <=
                  std::min(std::max(lat1, lat2),
                           std::max(seg.lat1, seg.lat2)) &&
              std::max(std::min(lon1, lon2), std::min(seg.lon1, seg.lon2)) <=
                  std::min(std::max(lon1, lon2), std::max(seg.lon1, seg.lon2)))
            return 1;
        }
      }
    return 0;
  } catch (...) {
    tracks.clear();
    tracksLoaded = false;
    return -1;
  }
}
}  // namespace weather_routing
