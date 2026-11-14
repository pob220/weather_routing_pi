#include <gtest/gtest.h>
#include <wx/filename.h>
#include <fstream>

#include "weather_routing_pi.h"
#include "Boat.h"
#include "tinyxml.h"

namespace {
wxString boatTestPath;

class BoatXML : public testing::Test {
protected:
  void SetUp() override {
    boatTestPath = wxFileName::CreateTempFileName("wr-boat-xml-");
    ASSERT_FALSE(boatTestPath.empty());
    ASSERT_TRUE(wxRemoveFile(boatTestPath));
    ASSERT_TRUE(wxFileName::Mkdir(boatTestPath, wxS_DIR_DEFAULT));
    boatTestPath += wxFileName::GetPathSeparator();
  }
  void TearDown() override {
    wxFileName::Rmdir(boatTestPath, wxPATH_RMDIR_RECURSIVE);
    boatTestPath.clear();
  }
  void LoadPolars(Boat& boat) {
    for (const char* name : {"Example-0-10.pol", "Example-15-30.pol"}) {
      Polar polar;
      wxString message;
      ASSERT_TRUE(polar.Open(wxString(WEATHER_ROUTING_SOURCE_DIR) +
          "/data/polars/Example/" + name, message)) << message;
      boat.Polars.push_back(polar);
    }
  }
};
}  // namespace

// Boat.cpp uses this only to resolve its private polar/contour directory.
// Keep the production save/load implementation under test without a host UI.
wxString weather_routing_pi::StandardPath() { return boatTestPath; }

TEST_F(BoatXML, SaveReloadAndReorderKeepEachPolarsSettings) {
  Boat boat;
  ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  boat.Polars[0].lowWindPolicy = LowWindPolicy::Strict;
  boat.Polars[0].highWindPolicy = HighWindPolicy::Hold;
  boat.Polars[0].m_crossoverpercentage = .12;
  boat.Polars[1].lowWindPolicy = LowWindPolicy::Taper;
  boat.Polars[1].highWindPolicy = HighWindPolicy::Strict;
  boat.Polars[1].m_crossoverpercentage = .27;
  const wxString file = boatTestPath + "boat.xml";
  ASSERT_TRUE(boat.SaveXML(file).empty());
  Boat loaded;
  ASSERT_TRUE(loaded.OpenXML(file, false).empty());
  ASSERT_EQ(loaded.Polars.size(), 2U);
  // Reloading uses native path separators, including backslashes on Windows.
  for (std::size_t index = 0; index < 2; ++index) {
    EXPECT_TRUE(wxFileName(loaded.Polars[index].FileName).SameAs(
        wxFileName(boat.Polars[index].FileName)));
    EXPECT_EQ(loaded.Polars[index].lowWindPolicy, boat.Polars[index].lowWindPolicy);
    EXPECT_EQ(loaded.Polars[index].highWindPolicy, boat.Polars[index].highWindPolicy);
    EXPECT_NEAR(loaded.Polars[index].m_crossoverpercentage,
                boat.Polars[index].m_crossoverpercentage, 1e-8);
  }
  EXPECT_EQ(loaded.FindBestPolarForCondition(-1, 35, 60, 0, false), 0);
  std::swap(loaded.Polars[0], loaded.Polars[1]);
  ASSERT_TRUE(loaded.SaveXML(file).empty());
  Boat reordered;
  ASSERT_TRUE(reordered.OpenXML(file, false).empty());
  ASSERT_EQ(reordered.Polars.size(), 2U);
  EXPECT_TRUE(wxFileName(reordered.Polars[0].FileName).SameAs(
      wxFileName(boat.Polars[1].FileName)));
  EXPECT_EQ(reordered.Polars[0].lowWindPolicy, LowWindPolicy::Taper);
  EXPECT_EQ(reordered.Polars[0].highWindPolicy, HighWindPolicy::Strict);
  EXPECT_TRUE(wxFileName(reordered.Polars[1].FileName).SameAs(
      wxFileName(boat.Polars[0].FileName)));
  EXPECT_EQ(reordered.Polars[1].lowWindPolicy, LowWindPolicy::Strict);
  EXPECT_EQ(reordered.Polars[1].highWindPolicy, HighWindPolicy::Hold);
}

TEST_F(BoatXML, LegacyBoatRetainsMultiplePolarsAndAutomaticDefaults) {
  Boat boat;
  ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  const wxString file = boatTestPath + "legacy.xml";
  std::ofstream xml(file.ToStdString());
  xml << "<OpenCPNWeatherRoutingBoat>\n";
  for (const auto& polar : boat.Polars)
    xml << "<Polar FileName=\"" << polar.FileName.ToStdString()
        << "\" CrossOverPercentage=\"20\"/>\n";
  xml << "</OpenCPNWeatherRoutingBoat>\n";
  xml.close();
  Boat loaded;
  ASSERT_TRUE(loaded.OpenXML(file, false).empty());
  ASSERT_EQ(loaded.Polars.size(), 2U);
  // Reloading uses native path separators, including backslashes on Windows.
  for (std::size_t index = 0; index < 2; ++index) {
    EXPECT_TRUE(wxFileName(loaded.Polars[index].FileName).SameAs(
        wxFileName(boat.Polars[index].FileName)));
    EXPECT_EQ(loaded.Polars[index].lowWindPolicy, LowWindPolicy::Automatic);
    EXPECT_EQ(loaded.Polars[index].highWindPolicy, HighWindPolicy::Automatic);
    EXPECT_NEAR(loaded.Polars[index].m_crossoverpercentage, .2, 1e-8);
  }
}

TEST_F(BoatXML, MultiplePolarSelectionRespectsSailWindLimits) {
  Boat boat;
  ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  EXPECT_EQ(boat.FindBestPolarForCondition(-1, 5, 60, 0, false), 0);
  EXPECT_EQ(boat.FindBestPolarForCondition(0, 18.2, 60, 0, false), 1);
  EXPECT_EQ(boat.FindBestPolarForCondition(1, 12, 60, 0, false), -1);
  EXPECT_EQ(boat.FindBestPolarForCondition(1, 35, 60, 0, false), -1);
}

TEST_F(BoatXML, ImportCopiesAllPolarsAndLeavesOriginalUntouched) {
  Boat boat;ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  boat.Polars[0].lowWindPolicy=LowWindPolicy::Strict;
  boat.Polars[1].highWindPolicy=HighWindPolicy::Hold;
  const wxString original=boatTestPath+"original.xml";
  ASSERT_TRUE(boat.SaveXML(original).empty());
  auto read=[](const wxString& p){std::ifstream in(p.ToStdString(),std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
  const auto before=read(original);
  const wxString destination=boatTestPath+"version2";
  wxString imported;ASSERT_TRUE(Boat::ImportXML(original,destination,imported).empty());
  EXPECT_EQ(read(original),before);
  ASSERT_NE(original,imported);
  TiXmlDocument doc(imported.utf8_str());ASSERT_TRUE(doc.LoadFile());
  int index=0;
  for(auto* e=doc.RootElement()->FirstChildElement("Polar");e;e=e->NextSiblingElement("Polar")) {
    ASSERT_LT(index,2);
    const wxString path=wxString::FromUTF8(e->Attribute("FileName"));
    EXPECT_TRUE(path.StartsWith(destination));
    EXPECT_EQ(read(path),read(boat.Polars[index].FileName));
    EXPECT_EQ(e->Attribute("CrossOverContours"),nullptr);
    EXPECT_STREQ(e->Attribute(index?"HighWindPolicy":"LowWindPolicy"),index?"hold":"strict");
    ++index;
  }
  EXPECT_EQ(index,2);
}

TEST_F(BoatXML, SuccessfulLoadRetainsPolarWarningsAndReloadClearsRepairedWarnings) {
  const wxString polarFile = boatTestPath + "warning.pol";
  auto writePolar = [&](bool badRow) {
    std::ofstream out(polarFile.ToStdString());
    out << "twa/tws;5;10\n30;3;4\n";
    if (badRow) out << "-1;1;2\n";
    out << "60;4;5\n120;4;5\n";
  };
  writePolar(true);
  // Use a real, empty cache so this warning-only fixture does not emit a
  // missing-file error (wxWidgets can display it as a modal dialog on Windows).
  const wxString contoursFile = boatTestPath + "empty.contours";
  {
    std::ofstream out(contoursFile.ToStdString(), std::ios::binary);
    ASSERT_TRUE(out);
  }
  TiXmlDocument document;
  auto* node = new TiXmlElement("OpenCPNWeatherRoutingBoat");
  auto* polar = new TiXmlElement("Polar");
  polar->SetAttribute("FileName", polarFile.ToUTF8().data());
  polar->SetAttribute("CrossOverContours", contoursFile.ToUTF8().data());
  node->LinkEndChild(polar);
  document.LinkEndChild(node);
  const wxString boatFile = boatTestPath + "warning.xml";
  ASSERT_TRUE(document.SaveFile(boatFile.ToUTF8().data()));
  Boat boat;
  ASSERT_TRUE(boat.OpenXML(boatFile, false).empty());
  ASSERT_EQ(boat.Polars.size(), 1U);
  EXPECT_FALSE(boat.loadWarnings.empty());
  EXPECT_NE(boat.loadWarnings.Find("line"), wxNOT_FOUND);
  ASSERT_TRUE(boat.OpenXML(boatFile).empty());
  EXPECT_FALSE(boat.loadWarnings.empty());
  writePolar(false);
  ASSERT_TRUE(boat.OpenXML(boatFile, false).empty());
  EXPECT_TRUE(boat.loadWarnings.empty());
}

TEST_F(BoatXML, OptionalMetadataSurvivesSaveImportAndReordering) {
  Boat boat; ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  boat.Name = "My boat"; boat.Description = "Cruising performance";
  boat.Polars[0].Name = "Working sails";
  boat.Polars[0].Source = "Manufacturer estimate";
  boat.Polars[0].SailConfiguration = "Reefed main and jib";
  boat.Polars[0].Notes = "Review against trials";
  boat.Polars[0].Modified = "2026-11-07T15:00:00Z";
  const wxString source = boatTestPath + "metadata.xml";
  ASSERT_TRUE(boat.SaveXML(source).empty());
  wxString imported;
  ASSERT_TRUE(Boat::ImportXML(source, boatTestPath + "personal", imported).empty());
  Boat copy; ASSERT_TRUE(copy.OpenXML(imported, false, false).empty());
  ASSERT_EQ(copy.Polars.size(), 2U);
  EXPECT_EQ(copy.Name, boat.Name); EXPECT_EQ(copy.Description, boat.Description);
  EXPECT_EQ(copy.Polars[0].Name, boat.Polars[0].Name);
  EXPECT_EQ(copy.Polars[0].Source, boat.Polars[0].Source);
  EXPECT_EQ(copy.Polars[0].SailConfiguration, boat.Polars[0].SailConfiguration);
  EXPECT_EQ(copy.Polars[0].Modified, boat.Polars[0].Modified);
  EXPECT_TRUE(copy.Polars[1].Source.empty());
  std::swap(copy.Polars[0], copy.Polars[1]);
  ASSERT_TRUE(copy.SaveXML(imported).empty());
  Boat reordered; ASSERT_TRUE(reordered.OpenXML(imported, false, false).empty());
  EXPECT_EQ(reordered.Polars[1].Source, boat.Polars[0].Source);
  EXPECT_TRUE(reordered.Polars[0].Source.empty());
}
TEST_F(BoatXML, ExportIsPortableAndDoesNotRewriteTheOriginal) {
  Boat boat; ASSERT_NO_FATAL_FAILURE(LoadPolars(boat));
  const wxString source = boatTestPath + "original.xml";
  ASSERT_TRUE(boat.SaveXML(source).empty());
  wxFile input(source); wxString before; ASSERT_TRUE(input.ReadAll(&before)); input.Close();
  const wxString target = boatTestPath + "shared.xml";
  ASSERT_TRUE(Boat::ExportXML(source, target).empty());
  wxFile unchanged(source); wxString after; ASSERT_TRUE(unchanged.ReadAll(&after));
  EXPECT_EQ(before, after);
  TiXmlDocument doc; ASSERT_TRUE(doc.LoadFile(target.utf8_str()));
  auto* polar = doc.RootElement()->FirstChildElement("Polar"); ASSERT_NE(polar, nullptr);
  EXPECT_TRUE(wxFileName(wxString::FromUTF8(polar->Attribute("FileName"))).IsRelative());
  Boat exported; ASSERT_TRUE(exported.OpenXML(target, false, false).empty());
  ASSERT_EQ(exported.Polars.size(), 2U);
  for (const auto& p : exported.Polars) {
    EXPECT_TRUE(wxFileExists(p.FileName));
    EXPECT_TRUE(p.FileName.StartsWith(boatTestPath));
  }
}
