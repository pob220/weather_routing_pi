#pragma once

#include <wx/jsonval.h>

// Create the versioned fields shared by every native generator job. Explicit
// wxString values avoid wxJSON's const-char-to-bool overload trap.
wxJSONValue CreateGeneratorJobEnvelope();

// Read only a completed v1 job's full final-output numeric inventory.
// Older helpers and partial/malformed inventories deliberately return false.
bool ReadGeneratorActualNumericMiB(wxJSONValue jobResult, double* mib);
