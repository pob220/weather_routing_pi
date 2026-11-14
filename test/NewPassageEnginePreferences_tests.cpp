// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <wx/fileconf.h>
#include <wx/sstream.h>
#include "NewPassageEnginePreferences.h"

namespace wr = weather_routing;
namespace {
constexpr auto path = "/PlugIns/xWeatherRouting2/LastUsedConfiguration";
void ExpectEveryEngine(const wr::RoutingEngineSettings& settings) {
  for (const auto engine : wr::kSequenceEngines) {
    EXPECT_NE(settings.autoEngines & wr::EngineBit(engine), 0U);
    EXPECT_NE(settings.allEngines & wr::EngineBit(engine), 0U);
  }
}
}  // namespace
TEST(NewPassageEnginePreferences,
     FreshAndUnavailableConfigStartAutoWithEveryEngine) {
  const auto noConfig = wr::ReadNewPassageEnginePreferences(nullptr);
  EXPECT_EQ(noConfig.engine, wr::RoutingEngine::Auto);
  ExpectEveryEngine(noConfig);
  wxStringInputStream input("");
  wxFileConfig config(input);
  config.SetPath(path);
  const auto empty = wr::ReadNewPassageEnginePreferences(&config);
  EXPECT_EQ(empty.engine, wr::RoutingEngine::Auto);
  ExpectEveryEngine(empty);
}
TEST(NewPassageEnginePreferences,
     UnrelatedSavedBoatDoesNotSelectLegacyProfessionalOrOldMasks) {
  wxStringInputStream input(
      "[PlugIns/xWeatherRouting2/LastUsedConfiguration]\nBoat=personal.xml\n");
  wxFileConfig config(input);
  config.SetPath(path);
  const auto settings = wr::ReadNewPassageEnginePreferences(&config);
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Auto);
  ExpectEveryEngine(settings);
  EXPECT_EQ(config.Read("Boat", ""), "personal.xml");
  EXPECT_FALSE(
      config.HasEntry("RoutingEngine"));  // Loading does not save choices.
}
TEST(NewPassageEnginePreferences,
     ChoicesCarryToNextRouteAndRestartForEveryMode) {
  for (const auto engine : wr::kEngineChoices) {
    wxStringInputStream input("");
    wxFileConfig config(input);
    config.SetPath(path);
    auto changed = wr::ReadNewPassageEnginePreferences(&config);
    changed.engine = engine;
    changed.autoEngines = wr::EngineBit(wr::RoutingEngine::Atlas) |
                          wr::EngineBit(wr::RoutingEngine::Quick2);
    changed.allEngines = wr::EngineBit(wr::RoutingEngine::Original) |
                         wr::EngineBit(wr::RoutingEngine::Alternative);
    changed.quick.memoryBudgetMiB = 768;
    wr::WriteRoutingEngineSettings(changed, config);
    EXPECT_EQ(wr::ReadNewPassageEnginePreferences(&config), changed);
    wxStringOutputStream saved;
    ASSERT_TRUE(config.Save(saved));
    wxStringInputStream restoredInput(saved.GetString());
    wxFileConfig restored(restoredInput);
    restored.SetPath(path);
    EXPECT_EQ(wr::ReadNewPassageEnginePreferences(&restored), changed);
  }
}
TEST(NewPassageEnginePreferences,
     IndependentMasksAndExplicitEmptySelectionStaySaved) {
  wxStringInputStream input(
      "[PlugIns/xWeatherRouting2/LastUsedConfiguration]\nAutoEngineMask=0\n");
  wxFileConfig config(input);
  config.SetPath(path);
  auto settings = wr::ReadNewPassageEnginePreferences(&config);
  EXPECT_EQ(settings.engine, wr::RoutingEngine::Auto);
  EXPECT_EQ(settings.autoEngines, 0U);
  EXPECT_EQ(settings.allEngines, wr::kSelectableEnginesMask);
  config.DeleteEntry("AutoEngineMask");
  config.Write("AllEngineMask", 8L);
  settings = wr::ReadNewPassageEnginePreferences(&config);
  EXPECT_EQ(settings.autoEngines, wr::kSelectableEnginesMask);
  EXPECT_EQ(settings.allEngines, 8U);
}
TEST(NewPassageEnginePreferences, LegacySavedChoicesAndRouteXmlArePreserved) {
  wxStringInputStream input(
      "[PlugIns/xWeatherRouting2/"
      "LastUsedConfiguration]\nQuickRoute=1\nAutoEngineMask=23\nAllEngineMask="
      "31\n");
  wxFileConfig config(input);
  config.SetPath(path);
  const auto saved = wr::ReadNewPassageEnginePreferences(&config);
  EXPECT_EQ(saved.engine, wr::RoutingEngine::Quick);
  EXPECT_EQ(saved.autoEngines, 23U);
  EXPECT_EQ(saved.allEngines, 31U);
  TiXmlElement legacy("Configuration");
  const auto route = wr::ReadRoutingEngineSettings(legacy);
  EXPECT_EQ(route.engine, wr::RoutingEngine::Main);
  EXPECT_EQ(route.autoEngines, 23U);
  EXPECT_EQ(route.allEngines, 31U);
}

TEST(NewPassageEnginePreferences,
     NewRouteDoesNotInheritIncidentalOldSelectorOrResetSearchValues) {
  wr::RoutingEngineSettings copied;
  copied.engine = wr::RoutingEngine::Original;
  copied.quick.memoryBudgetMiB = 768;
  copied.original.offshoreStepMinutes = 240;
  const auto search = copied;
  wxStringInputStream input(
      "[PlugIns/xWeatherRouting2/LastUsedConfiguration]\nBoat=personal.xml\n");
  wxFileConfig config(input);
  config.SetPath(path);
  wr::ApplyNewPassageEngineChoices(copied, &config);
  EXPECT_EQ(copied.engine, wr::RoutingEngine::Auto);
  ExpectEveryEngine(copied);
  EXPECT_EQ(copied.quick, search.quick);
  EXPECT_EQ(copied.original, search.original);
  EXPECT_EQ(copied.mainPreset, search.mainPreset);
  config.Write("RoutingEngine", "all");
  config.Write("AutoEngineMask", 5L);
  config.Write("AllEngineMask", 32L);
  wr::ApplyNewPassageEngineChoices(copied, &config);
  EXPECT_EQ(copied.engine, wr::RoutingEngine::All);
  EXPECT_EQ(copied.autoEngines, 5U);
  EXPECT_EQ(copied.allEngines, 32U);
  EXPECT_EQ(copied.quick, search.quick);
  EXPECT_EQ(copied.original, search.original);
}
