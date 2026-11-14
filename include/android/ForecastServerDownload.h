// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from xGRIB 0.4.5, source a5606ce; scoped to Weather Routing.
#pragma once

#include <cstdio>
#include <fcntl.h>
#include <locale>
#include <sstream>
#include <unistd.h>
#include "environmental_grib/environment.h"
#include "environmental_grib/grib.h"

namespace xgrib {
// Same service, model IDs and durations as the desktop Download GRIB tab.
inline std::string ServerGribUrl(const environmental_grib::BoundingBox& bbox,
                                 const std::string& model, int hours) {
  bbox.Validate();
  if (model != "ecmwf0p25" && model != "ecmwfaifs0p25")
    throw std::runtime_error("Unsupported server model");
  if (hours != 24 && hours != 72 && hours != 999)
    throw std::runtime_error("Unsupported server forecast duration");
  std::ostringstream url; url.imbue(std::locale::classic());
  url << "https://grib.bosun.io/grib?model=" << model
      << "&latmin=" << bbox.south << "&latmax=" << bbox.north
      << "&lonmin=" << bbox.west << "&lonmax=" << bbox.east << "&length=" << hours;
  return url.str();
}

inline environmental_grib::EnvironmentResult DownloadServerGrib(
    const environmental_grib::EnvironmentRequest& request,
    const std::string& model, int hours) {
  namespace eg = environmental_grib;
  const auto url = ServerGribUrl(request.bbox, model, hours);
  eg::ExecutionScope scope(request.execution);
  eg::EnsureHttpInitialized();
  eg::CheckCancellation();
  if (std::filesystem::exists(request.output))
    throw std::runtime_error("That output already exists. Choose another filename in Options.");
  const auto partial = request.output.string() + ".part";
  // Exclusive creation and copy_file(no overwrite) protect existing forecasts.
  const int fd = ::open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) throw std::runtime_error("Cannot create download file; choose a new filename");
  struct Partial {
    std::string path;
    FILE* file;
    ~Partial() { if (file) std::fclose(file); std::error_code ec; std::filesystem::remove(path, ec); }
  } output{partial, fdopen(fd, "wb")};
  if (!output.file) { ::close(fd); throw std::runtime_error("Cannot open download file"); }
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
  if (!curl) throw std::runtime_error("Cannot initialise download");
  struct Sink { FILE* file; size_t size{0}; } sink{output.file};
  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
  curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
  curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 600L);
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
  eg::ConfigureJobCurl(curl.get());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &sink);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
      +[](char* data, size_t size, size_t count, void* context) -> size_t {
        auto& sink = *static_cast<Sink*>(context);
        // Keep unexpectedly large/error responses off a memory-limited device.
        const size_t remaining = 128 * 1024 * 1024 - sink.size;
        if (size && count > remaining / size) return 0;
        const size_t bytes = size * count;
        const size_t written = std::fwrite(data, 1, bytes, sink.file);
        sink.size += written; return written;
      });
  const auto code = curl_easy_perform(curl.get());
  eg::CheckCancellation();
  if (code != CURLE_OK)
    throw std::runtime_error(std::string("Server download failed (128 MiB limit): ") + curl_easy_strerror(code));
  const int closed = std::fclose(output.file); output.file = nullptr;
  if (closed) throw std::runtime_error("Unable to finish writing GRIB download");
  auto inspection = eg::InspectGrib(partial);
  const auto count = inspection["message_count"].asUInt64();
  if (!count) throw std::runtime_error("The server did not return a usable GRIB file");
  eg::CheckCancellation();
  std::filesystem::copy_file(partial, request.output);  // throws if target exists
  eg::EnvironmentResult result;
  result.output = request.output; result.message_count = count;
  result.byte_count = sink.size; result.inspection = std::move(inspection);
  return result;
}
}
