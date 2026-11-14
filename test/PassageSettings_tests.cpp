// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <limits>
#include "PassageSettings.h"
namespace wr = weather_routing;
using S = wr::PassageSetting;

TEST(PassageSettings, ZeroHasFieldSpecificMeaningInTheUi) {
  EXPECT_EQ("Off", wr::PassageSettingValue(S::TrueWind, 0));
  EXPECT_EQ("Off", wr::PassageSettingValue(S::MinimumDepth, 0));
  EXPECT_EQ("Auto", wr::PassageSettingValue(S::DepartureWorkers, 0));
  EXPECT_EQ("0%", wr::PassageSettingValue(S::UpwindFactor, 0));
  EXPECT_EQ("100%", wr::PassageSettingValue(S::NightFactor, 1));
  EXPECT_FALSE(wr::CheckPassageSetting(S::TimeStep, 0).empty());
}
TEST(PassageSettings, WindRelativeAnglesUseActualEngineLimits) {
  EXPECT_TRUE(wr::CheckPassageSetting(S::SailingTo, 180).empty());
  EXPECT_FALSE(wr::CheckPassageSetting(S::SailingTo, 360).empty());
  EXPECT_TRUE(wr::CheckPassageSetting(S::SailingIncrement, .1).empty());
  EXPECT_TRUE(wr::CheckPassageSetting(S::SailingIncrement, 60).empty());
  EXPECT_FALSE(wr::CheckPassageSetting(S::SailingIncrement, .09).empty());
  EXPECT_FALSE(wr::CheckPassageSetting(S::SailingIncrement, 61).empty());
}
TEST(PassageSettings, DisabledMotoringDoesNotInvalidateStoredInactiveValues) {
  EXPECT_TRUE(wr::CheckMotorSettings(false, -1, 0).empty());
  EXPECT_FALSE(wr::CheckMotorSettings(true, 2, 0).empty());
  EXPECT_FALSE(wr::CheckMotorSettings(true, -1, 5).empty());
  EXPECT_TRUE(wr::CheckMotorSettings(true, 0, 5).empty());
}
TEST(PassageSettings, AutoWorkersAndFiniteMultipliersHaveNoInventedCaps) {
  for (const double value : {0., 1., 12.})
    EXPECT_TRUE(wr::CheckPassageSetting(S::DepartureWorkers, value).empty());
  for (const double value : {-1., 1.5, 13.})
    EXPECT_FALSE(wr::CheckPassageSetting(S::DepartureWorkers, value).empty());
  EXPECT_TRUE(wr::CheckPassageSetting(S::UpwindFactor, 2.5).empty());
  EXPECT_FALSE(wr::CheckPassageSetting(S::TrueWind, std::numeric_limits<double>::infinity()).empty());
  EXPECT_FALSE(wr::CheckPassageSetting(S::NightFactor, std::numeric_limits<double>::quiet_NaN()).empty());
}
