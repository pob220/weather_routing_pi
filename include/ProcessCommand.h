#pragma once

#include <wx/string.h>
#include <wx/utils.h>

namespace xgrib {

// Quote one argument for the native command-line parser used by wxExecute.
// Windows CreateProcess uses double-quote/backslash rules; POSIX uses the
// shell's single-quote rules.
wxString QuoteProcessArgument(const wxString& value);

// The generator is a console helper: wxSIGTERM posts a window message on
// Windows, so use native forced termination there instead.
void TerminateGeneratorProcess(long pid, wxKillError* error = nullptr);

}  // namespace xgrib
