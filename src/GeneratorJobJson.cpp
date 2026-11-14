#include "GeneratorJobJson.h"

#include <wx/string.h>
#include <cmath>

wxJSONValue CreateGeneratorJobEnvelope() {
  wxJSONValue root;
  root["schemaVersion"] = 1;
  root["operation"] = wxString("generateEnvironment");
  root["request"]["cycle"] = wxString("auto");
  root["credentials"]["copernicusPasswordEnvironment"] =
      wxString("ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD");
  return root;
}

bool ReadGeneratorActualNumericMiB(wxJSONValue jobResult, double* mib) {
  if (!mib || !jobResult["schemaVersion"].IsInt() ||
      jobResult["schemaVersion"].AsInt() != 1 ||
      jobResult["status"].AsString() != "complete") return false;
  auto report = jobResult["result"]["size_comparison"];
  auto actual = report["actual"];
  if (!report["schemaVersion"].IsInt() || report["schemaVersion"].AsInt() != 1 ||
      !actual["numericComplete"].IsBool() || !actual["numericComplete"].AsBool())
    return false;
  const auto& bytes = actual["decodedBytes"];
  double value = 0;
  if (!(bytes.IsInt64() || bytes.IsUInt64()) ||
      !bytes.AsString().ToDouble(&value) || !std::isfinite(value) || value < 0)
    return false;
  *mib = value / 1048576.0;
  return true;
}
