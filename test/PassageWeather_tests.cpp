// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PassageWeather.h"
#include "BundledClimatology.h"
#include <wx/datetime.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <thread>
#include <cmath>
#include <fstream>
#include <sstream>
#include <array>
#include <algorithm>
#include <iterator>
#include <vector>

namespace {
const wxString source = wxString::FromUTF8(WEATHER_ROUTING_SOURCE_DIR);
wxDateTime Date(const char* s) {
  wxDateTime t;
  t.ParseISOCombined(s);
  return t;
}
TEST(PassageWeather, LoadsSurfaceWindWithoutAnotherPlugin) {
  weather_routing::PassageWeather w;
  wxString error;
  ASSERT_TRUE(w.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb", error))
      << error;
  EXPECT_LE(w.FirstWindTime(), w.LastWindTime());
  EXPECT_TRUE(w.HasWind(w.FirstWindTime(), 53.4, -5.5));
  EXPECT_FALSE(w.HasWind(w.FirstWindTime(), -50, 100));
  EXPECT_FALSE(w.HasWind(w.FirstWindTime() - 1, 53.4, -5.5));
  EXPECT_FALSE(w.HasWind(w.LastWindTime() + 1, 53.4, -5.5));
}
TEST(PassageWeather, AdvisorySamplesAgreeWithResidentFramesWithoutChangingSource) {
  weather_routing::PassageWeather weather; wxString error;
  ASSERT_TRUE(weather.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb",error));
  unsigned checked=0;
  const auto first=weather.FirstWindTime(), last=weather.LastWindTime();
  for (auto time:{first,first+(last-first)/3,first+2*(last-first)/3,last}) {
    const auto frame=weather.Frame(time);
    for (const auto index:{Idx_WIND_VX,Idx_WIND_VY}) {
      auto* field=frame->m_GribRecordPtrArray[index]; ASSERT_NE(field,nullptr);
      const double expected=field->getInterpolatedValue(-5.5,53.4);
      const auto sample=weather.Sample(index,time,53.4,-5.5);
      ASSERT_TRUE(sample); EXPECT_NEAR(*sample,expected,1e-9); ++checked;
    }
  }
  EXPECT_EQ(checked,8U);
  EXPECT_FALSE(weather.Sample(Idx_WIND_VX,first-1,53.4,-5.5));
  EXPECT_FALSE(weather.Sample(Idx_WIND_VX,last+1,53.4,-5.5));
  EXPECT_FALSE(weather.Sample(Idx_WIND_VX,first,-50,100));
  EXPECT_FALSE(weather.Sample(-1,first,53.4,-5.5));
}
TEST(PassageWeather, FailedReplacementKeepsSelectedForecast) {
  weather_routing::PassageWeather w;
  wxString error;
  ASSERT_TRUE(
      w.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb", error));
  const auto path = w.Path();
  EXPECT_FALSE(w.Open(source + "/data/boats/Boat.xml", error));
  EXPECT_EQ(w.Path(), path);
  EXPECT_TRUE(w.HasWind(w.FirstWindTime(), 53.4, -5.5));
}
#ifdef JASPER
TEST(PassageWeather, Jpeg2000WindAndMaskedWavesMatchIndependentDecoder) {
  const auto fixture = source + "/ci/testdata/farallon-jpeg2000.grb2";
  GribReader reader(fixture);
  ASSERT_TRUE(reader.isOk());
  ASSERT_EQ(reader.getTotalNumberOfGribRecords(), 7);
  std::ifstream reference(std::string(
      (source + "/ci/testdata/farallon-jpeg2000-values.txt").ToUTF8()));
  ASSERT_TRUE(reference);
  std::string line;
  int checked = 0, missing = 0;
  while (std::getline(reference, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    int type;
    time_t time;
    double lat, lon, expected;
    ASSERT_TRUE(bool(row >> type >> time >> lat >> lon >> expected));
    const bool wind = type == GRB_WIND_VX || type == GRB_WIND_VY;
    auto* record = reader.getGribRecord(
        type, wind ? LV_ABOV_GND : LV_GND_SURF, wind ? 10 : 0, time);
    ASSERT_NE(record, nullptr) << line;
    ASSERT_EQ(record->getNi(), 20);
    ASSERT_EQ(record->getNj(), 9);
    lon -= 360 * std::round((lon - record->getX(0)) / 360);
    const int x = std::lround((lon - record->getX(0)) / record->getDi());
    const int y = std::lround((lat - record->getY(0)) / record->getDj());
    ASSERT_GE(x, 0);
    ASSERT_LT(x, record->getNi());
    ASSERT_GE(y, 0);
    ASSERT_LT(y, record->getNj());
    EXPECT_NEAR(record->getX(x), lon, 1e-6);
    EXPECT_NEAR(record->getY(y), lat, 1e-6);
    if (expected == GRIB_NOTDEF) {
      EXPECT_EQ(record->getValue(x, y), GRIB_NOTDEF);
      ++missing;
    } else {
      EXPECT_NEAR(record->getValue(x, y), expected, 1e-7) << line;
    }
    ++checked;
  }
  EXPECT_EQ(checked, 1260);
  EXPECT_EQ(missing, 204);
  weather_routing::PassageWeather forecast;
  wxString error;
  ASSERT_TRUE(forecast.Open(fixture, error)) << error;
  EXPECT_TRUE(forecast.HasWind(1791892800, 37.5, -123.0));
  EXPECT_TRUE(forecast.HasWind(1791898200, 37.5, -123.0));
}

TEST(PassageWeather, Jpeg2000RepeatedAndConcurrentWorkerLoads) {
  std::array<bool, 4> loaded{};
  std::array<std::thread, 4> workers;
  for (std::size_t i = 0; i < workers.size(); ++i)
    workers[i] = std::thread([&, i] {
      loaded[i] = true;
      for (int repeat = 0; repeat < 3; ++repeat) {
        weather_routing::PassageWeather forecast;
        wxString error;
        if (!forecast.Open(source + "/ci/testdata/farallon-jpeg2000.grb2", error) ||
            !forecast.HasWind(1791892800, 37.5, -123.0))
          loaded[i] = false;
      }
    });
  for (auto& worker : workers) worker.join();
  for (bool ok : loaded) EXPECT_TRUE(ok);
}

TEST(PassageWeather, DamagedJpeg2000KeepsForecastAndAllowsSubsequentLoad) {
  const auto fixture = source + "/ci/testdata/farallon-jpeg2000.grb2";
  std::ifstream input(std::string(fixture.ToUTF8()), std::ios::binary);
  ASSERT_TRUE(input);
  std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
  const std::array<unsigned char, 4> socsiz{{0xff, 0x4f, 0xff, 0x51}};
  auto cursor = bytes.begin();
  int damaged = 0;
  while ((cursor = std::search(cursor, bytes.end(), socsiz.begin(), socsiz.end(),
                             [](char a, unsigned char b) {
                               return static_cast<unsigned char>(a) == b;
                             })) != bytes.end()) {
    // Keep the SOC/SIZ geometry intact and damage the following marker, so
    // this exercises codec failure and cleanup after successful initialization.
    ASSERT_GE(bytes.end() - cursor, 47);
    const auto sizLength = (static_cast<unsigned char>(cursor[4]) << 8) |
                           static_cast<unsigned char>(cursor[5]);
    ASSERT_LT(sizLength + 4, bytes.end() - cursor);
    cursor[sizLength + 4] = 0;
    cursor += sizLength + 5;
    ++damaged;
  }
  ASSERT_EQ(damaged, 7);
  struct TemporaryFile {
    wxString path = wxFileName::CreateTempFileName("wr-jpeg2000-");
    ~TemporaryFile() { wxRemoveFile(path); }
  } broken;
  {
    std::ofstream output(std::string(broken.path.ToUTF8()), std::ios::binary);
    output.write(bytes.data(), bytes.size());
    ASSERT_TRUE(output);
  }
  weather_routing::PassageWeather forecast;
  wxString error;
  ASSERT_TRUE(forecast.Open(fixture, error)) << error;
  const auto identity = forecast.Identity();
  EXPECT_FALSE(forecast.Open(broken.path, error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(forecast.Identity(), identity);
  EXPECT_TRUE(forecast.HasWind(1791892800, 37.5, -123.0));
  EXPECT_TRUE(forecast.Open(fixture, error)) << error;
}
#endif
TEST(PassageWeather, DeliveredFrameOwnsItsData) {
  std::unique_ptr<GribRecordSet> frame;
  {
    weather_routing::PassageWeather w;
    wxString error;
    ASSERT_TRUE(
        w.Open(source + "/ci/testdata/irish-sea-uniform-wind.grb", error));
    frame = w.Frame(w.FirstWindTime());
  }
  ASSERT_NE(frame->m_GribRecordPtrArray[Idx_WIND_VX], nullptr);
  EXPECT_TRUE(std::isfinite(
      frame->m_GribRecordPtrArray[Idx_WIND_VX]->getInterpolatedValue(-5.5,
                                                                     53.4)));
}
TEST(BundledClimatology, WorksOfflineAndPreservesMissingMasks) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  int count = 8;
  double f[8]{}, s[8]{}, gale, calm;
  EXPECT_TRUE(
      C::Atlas(Date("2026-11-07T08:00:00"), 30, -40, count, f, s, gale, calm));
  double sum = 0;
  for (int i = 0; i < 8; ++i) {
    EXPECT_GE(f[i], 0);
    EXPECT_GE(s[i], 0);
    sum += f[i];
  }
  EXPECT_NEAR(sum, 1, 0.08);
  EXPECT_GE(gale, 0);
  EXPECT_LE(gale, 1);
  EXPECT_GE(calm, 0);
  EXPECT_LE(calm, 1);
  double dir, speed;
  EXPECT_TRUE(C::Data(1, Date("2026-11-07T08:00:00"), 30, -40, dir, speed));
  EXPECT_GE(speed, 0);
  EXPECT_LT(dir, 360);
  EXPECT_FALSE(
      C::Atlas(Date("2026-11-07T08:00:00"), 0, 20, count, f, s, gale, calm));
  EXPECT_FALSE(C::Data(1, Date("2026-11-07T08:00:00"), 89, -40, dir, speed));
}
TEST(BundledClimatology, DatasetVersionTakesPrecedenceOverPluginVersion) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  const auto bundled = C::Resolve();
  EXPECT_NE(bundled.data, nullptr);
  EXPECT_NE(bundled.wind_atlas, nullptr);
  auto external = bundled;
  external.major = 20;
  external.dataset = "ocpn-climatology-2026.1";
  EXPECT_EQ(C::Resolve(external).dataset, "ocpn-climatology-2026.2");
  external.dataset = "ocpn-climatology-2026.3";
  EXPECT_EQ(C::Resolve(external).dataset, external.dataset);
  external.valid = false;
  EXPECT_EQ(C::Resolve(external).dataset, "ocpn-climatology-2026.2");
}
TEST(BundledClimatology, WorkerQueriesAndLongitudeWrapAreConsistent) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  double da, sa, db, sb;
  ASSERT_TRUE(C::Data(0, Date("2026-01-01T00:00:00"), 30, -40, da, sa));
  bool ok = false;
  std::thread worker(
      [&] { ok = C::Data(0, Date("2026-01-01T00:00:00"), 30, 320, db, sb); });
  worker.join();
  ASSERT_TRUE(ok);
  EXPECT_DOUBLE_EQ(da, db);
  EXPECT_DOUBLE_EQ(sa, sb);
}
TEST(BundledClimatology, CycloneIndexIsAvailableWithoutPlugin) {
  using C = weather_routing::BundledClimatology;
  C::Configure(source + "/data/climatology");
  EXPECT_EQ(C::Cyclones(50, -5, 50.1, -5.1, Date("2026-11-07T08:00:00"), 365),
            0);
}
}  // namespace
