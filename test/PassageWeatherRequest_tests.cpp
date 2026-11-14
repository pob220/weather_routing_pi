// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PassageWeatherRequest.h"
namespace wr = weather_routing;
namespace {
wxDateTime Utc(const char* text) {
  wxDateTime value;
  value.ParseISOCombined(text);
  value.MakeFromTimezone(wxDateTime::UTC);
  return value;
}
}
TEST(PassageWeatherRequest, MinuteDepartureIsCoveredByHourlyRequestWithWaves) {
  const auto departure = Utc("2026-11-08T16:01:00");
  const auto window = wr::BuildPassageWeatherWindow(departure, 54, 1, true);
  ASSERT_TRUE(window);
  EXPECT_FALSE(wr::IsHourlyWeatherTime(departure));
  EXPECT_EQ(window->start, Utc("2026-11-08T16:00:00"));
  EXPECT_TRUE(wr::IsHourlyWeatherTime(window->start));
  EXPECT_EQ(window->hours, 57);
  EXPECT_LE(window->start, departure);
  EXPECT_GE(window->start + wxTimeSpan::Hours(window->hours),
            departure + wxTimeSpan::Hours(54));
  EXPECT_EQ(departure, Utc("2026-11-08T16:01:00"));
}
TEST(PassageWeatherRequest, AlignedWindowDoesNotAcquireExtraForecastHours) {
  const auto departure = Utc("2026-11-08T16:00:00");
  const auto window = wr::BuildPassageWeatherWindow(departure, 54, 1, true);
  ASSERT_TRUE(window);
  EXPECT_EQ(window->start, departure);
  EXPECT_EQ(window->hours, 54);
}
TEST(PassageWeatherRequest, SecondsAndMidnightDoNotTruncateTheRequestedEnd) {
  const auto departure = Utc("2026-11-08T23:59:59");
  const auto window = wr::BuildPassageWeatherWindow(departure, 24, 6, true);
  ASSERT_TRUE(window);
  EXPECT_EQ(window->start, Utc("2026-11-08T23:00:00"));
  EXPECT_EQ(window->hours, 30);
  EXPECT_GE(window->start + wxTimeSpan::Hours(window->hours),
            departure + wxTimeSpan::Hours(24));
}
TEST(PassageWeatherRequest, LongWindowStillCoversDepartureAndLastRequestedTime) {
  const auto departure = Utc("2026-11-08T16:01:00");
  const auto window = wr::BuildPassageWeatherWindow(departure, 360, 24, true);
  ASSERT_TRUE(window);
  EXPECT_EQ(window->hours, 384);
  EXPECT_GE(window->start + wxTimeSpan::Hours(window->hours),
            departure + wxTimeSpan::Hours(360));
}
TEST(PassageWeatherRequest, InvalidInputDoesNotInventARequestTime) {
  EXPECT_FALSE(wr::BuildPassageWeatherWindow(wxInvalidDateTime, 54, 1, true));
  EXPECT_FALSE(wr::BuildPassageWeatherWindow(Utc("2026-11-08T16:00:00"), 0, 1, false));
  EXPECT_FALSE(wr::BuildPassageWeatherWindow(Utc("2026-11-08T16:00:00"), 54, 0, false));
  EXPECT_FALSE(wr::IsHourlyWeatherTime(wxInvalidDateTime));
}

TEST(PassageWeatherRequest, SubsecondDepartureAlsoKeepsTheLastRequestedInstant) {
  auto departure = Utc("2026-11-08T16:00:00");
  departure.SetMillisecond(500);
  const auto window = wr::BuildPassageWeatherWindow(departure, 24, 1, false);
  ASSERT_TRUE(window);
  EXPECT_FALSE(wr::IsHourlyWeatherTime(departure));
  EXPECT_EQ(window->hours, 25);
  EXPECT_GE(window->start + wxTimeSpan::Hours(window->hours),
            departure + wxTimeSpan::Hours(24));
}

TEST(PassageWeatherRequest, NwsAndAutoDownloadsRoundDownAndCoverOriginalEnd) {
  const auto start = Utc("2026-11-08T16:01:23");
  for (const auto* provider : {"copernicus_nws", "auto"}) {
    const auto window = wr::BuildWeatherDownloadWindow(start, 24, 1, false, provider);
    ASSERT_TRUE(window);
    EXPECT_EQ(window->start, Utc("2026-11-08T16:00:00"));
    EXPECT_EQ(window->hours, 25);
    EXPECT_GE(window->start + wxTimeSpan::Hours(window->hours),
              start + wxTimeSpan::Hours(24));
    const auto again = wr::BuildWeatherDownloadWindow(
        window->start, window->hours, 1, false, provider);
    ASSERT_TRUE(again);
    EXPECT_EQ(again->start, window->start);
    EXPECT_EQ(again->hours, window->hours);
  }
  EXPECT_EQ(start, Utc("2026-11-08T16:01:23"));
}

TEST(PassageWeatherRequest, OtherCurrentReadersKeepTheirExistingTimeHandling) {
  const auto start = Utc("2026-11-08T16:01:23");
  for (const auto* provider : {"none", "tpxo", "tpxo-cache", "offline-tidal",
                             "netcdf", "synthetic", "existing-file",
                             "copernicus_global", "copernicus_ibi",
                             "copernicus_mediterranean", "noaa_rtofs_global"}) {
    const auto window = wr::BuildWeatherDownloadWindow(start, 24, 1, false, provider);
    ASSERT_TRUE(window);
    EXPECT_EQ(window->start, start);
    EXPECT_EQ(window->hours, 24);
  }
  EXPECT_FALSE(wr::BuildWeatherDownloadWindow(wxInvalidDateTime, 24, 1, false, "auto"));
  EXPECT_FALSE(wr::BuildWeatherDownloadWindow(start, 0, 1, false, "tpxo"));
}
