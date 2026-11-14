#include "ProcessCommand.h"

namespace xgrib {

void TerminateGeneratorProcess(long pid, wxKillError* error) {
#ifdef _WIN32
  wxKill(pid, wxSIGKILL, error, wxKILL_CHILDREN);
#else
  wxKill(pid, wxSIGTERM, error, wxKILL_CHILDREN);
#endif
}

wxString QuoteProcessArgument(const wxString& value) {
#ifdef _WIN32
  wxString quoted = "\"";
  size_t backslashes = 0;
  for (const wxUniChar character : value) {
    if (character == '\\') {
      ++backslashes;
      continue;
    }

    const size_t count = character == '"' ? backslashes * 2 + 1 : backslashes;
    for (size_t index = 0; index < count; ++index) quoted += '\\';
    backslashes = 0;
    quoted += character;
  }

  // Backslashes immediately before the closing quote must be doubled or the
  // final backslash escapes that quote in the Microsoft argument parser.
  for (size_t index = 0; index < backslashes * 2; ++index) quoted += '\\';
  quoted += '"';
  return quoted;
#else
  wxString escaped(value);
  escaped.Replace("'", "'\\''");
  return "'" + escaped + "'";
#endif
}

}  // namespace xgrib
