// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "PassagePresentation.h"
#include <clocale>
#include <string>

namespace wr = weather_routing;
TEST(PassagePresentation, NamedRoutesKeepTheirUnicodeName) {
  EXPECT_EQ(wr::PassageDisplayName(L"Málaga to Brest", "route-guid", "001", "005"),
            wxString(L"Málaga to Brest"));
}
TEST(PassagePresentation, IdentifierFallbackUsesWholePassageEndpoints) {
  const wxString guid = "5936ffff-9305-48b4-9bbb-3a7608c00000";
  EXPECT_EQ(wr::PassageDisplayName(guid, guid, "001", "005"), "001 to 005");
  EXPECT_EQ(wr::PassageDisplayName("", guid, "001", "005"), "001 to 005");
  EXPECT_EQ(wr::PassageDisplayName("", guid, "", ""), "Unnamed passage");
  EXPECT_EQ(guid, "5936ffff-9305-48b4-9bbb-3a7608c00000");
}
TEST(PassagePresentation, LegAndDirectionUseUnicodeWithoutAnsiDecoding) {
  // Native Windows CI exercises the same helpers under a Western ANSI locale.
  const char* current = std::setlocale(LC_CTYPE, nullptr);
  const std::string saved = current ? current : "C";
  struct Restore { std::string name; ~Restore() { std::setlocale(LC_CTYPE, name.c_str()); } } restore{saved};
#ifdef __WXMSW__
  ASSERT_NE(std::setlocale(LC_CTYPE, ".1252"), nullptr);
#endif
  EXPECT_EQ(wr::PassageLegLabel("001 to 005", 1, 4), wxString(L"001 to 005 • leg 1/4"));
  EXPECT_EQ(wr::PassageDirection("001", "005"), wxString(L"001  →  005"));
}

TEST(PassagePresentation, EditableCoordinatesAreReadableAndPreserveExactValues) {
  EXPECT_EQ(wr::WaypointCoordinateText(53.4), "53.4");
  EXPECT_EQ(wr::WaypointCoordinateText(-5.3), "-5.3");
  EXPECT_EQ(wr::WaypointCoordinateText(50), "50");
  for (double value : {53.4, -5.3, 53.400000000001, 1e-9, -179.99999999999997}) {
    double parsed = 0;
    ASSERT_TRUE(wr::WaypointCoordinateText(value).ToDouble(&parsed));
    EXPECT_DOUBLE_EQ(parsed, value);
  }
}
