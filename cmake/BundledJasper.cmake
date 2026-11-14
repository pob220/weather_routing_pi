include(FetchContent)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Debian no longer provides a dependable Jasper development package.  Pin a
# minimal static build so catalogue packages do not depend on a distribution
# specific libjasper SONAME.
set(JAS_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
# Flatpak's standard builddir is inside its disposable source copy. Jasper's
# source and binary trees remain distinct under _deps, so this override does
# not permit an actual in-source Jasper build.
set(ALLOW_IN_SOURCE_BUILD ON CACHE BOOL "" FORCE)
set(JAS_ENABLE_CMAKE_PACKAGE_CONFIG OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LIBJPEG OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LIBHEIF OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_OPENGL OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_DOC OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_LATEX OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(JAS_ENABLE_MULTITHREADING_SUPPORT OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_BMP_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_JPG_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_HEIC_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_MIF_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_PGX_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_PNM_CODEC OFF CACHE BOOL "" FORCE)
set(JAS_INCLUDE_RAS_CODEC OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  xgrib_jasper
  URL
    https://codeload.github.com/jasper-software/jasper/tar.gz/63e106c80eb72af9fd4fa28772499ab0138b9994
    https://github.com/jasper-software/jasper/archive/63e106c80eb72af9fd4fa28772499ab0138b9994.tar.gz
  URL_HASH
    SHA256=b4354e819341dfe43824af3cc59b9e8c0963c000ca5e38ca6cf05b9b9479208c
)
FetchContent_GetProperties(xgrib_jasper)
if(NOT xgrib_jasper_POPULATED)
  FetchContent_Populate(xgrib_jasper)
  if(QT_ANDROID)
    # Jasper 4.2.9 caches 0L on the second CMake pass even when
    # JAS_STDC_VERSION is supplied on the first. Pin the NDK Clang C11 value
    # in this fetched source before configuring it.
    set(_jasper_cmake "${xgrib_jasper_SOURCE_DIR}/CMakeLists.txt")
    file(READ "${_jasper_cmake}" _jasper_text)
    set(_jasper_old "set(JAS_STDC_VERSION \"0L\" CACHE INTERNAL \"The value of __STDC_VERSION__.\")")
    set(_jasper_new "set(JAS_STDC_VERSION \"201112L\" CACHE INTERNAL \"The value of __STDC_VERSION__.\")")
    string(FIND "${_jasper_text}" "${_jasper_old}" _jasper_position)
    if(_jasper_position EQUAL -1)
      string(FIND "${_jasper_text}" "${_jasper_new}" _jasper_position)
      if(_jasper_position EQUAL -1)
        message(FATAL_ERROR "Pinned Jasper cross-compile setting has changed")
      endif()
    endif()
    string(REPLACE "${_jasper_old}" "${_jasper_new}" _jasper_text "${_jasper_text}")
    file(WRITE "${_jasper_cmake}" "${_jasper_text}")
  endif()
  add_subdirectory(
    "${xgrib_jasper_SOURCE_DIR}"
    "${xgrib_jasper_BINARY_DIR}"
    EXCLUDE_FROM_ALL
  )
endif()

# Upstream keeps these PRIVATE and expects every embedding target to add them.
# Publish the pinned build's headers to both the plugin and GRIB reader tests.
target_include_directories(libjasper BEFORE INTERFACE
  "${xgrib_jasper_BINARY_DIR}/src/libjasper/include"
  "${xgrib_jasper_SOURCE_DIR}/src/libjasper/include")
target_compile_definitions(libjasper INTERFACE LIBJASPER_STATIC_DEFINE)

set(XGRIB_JASPER_TARGET libjasper)
set(XGRIB_JASPER_LICENSE "${xgrib_jasper_SOURCE_DIR}/LICENSE.txt")
