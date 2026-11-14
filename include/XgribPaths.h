#pragma once

#include <wx/string.h>

// Returns the catalogue/plugin-manager data directory, including a trailing
// separator. This works for both core-bundled and user-installed plugins.
wxString GetXgribDataDirectory();
