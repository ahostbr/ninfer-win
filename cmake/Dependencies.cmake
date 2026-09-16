find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)
if(WIN32)
  # Declared in vcpkg.json and resolved through the vcpkg toolchain; the Find
  # module ships with vcpkg. PkgConfig::FFMPEG is synthesized so every consumer
  # links the same target name on both platforms.
  find_package(FFMPEG REQUIRED)
  message(STATUS "FFmpeg: ${FFMPEG_VERSION}")
  add_library(PkgConfig::FFMPEG INTERFACE IMPORTED)
  set_target_properties(PkgConfig::FFMPEG PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}")
  # FFMPEG_LIBRARIES carries optimized/debug keywords from
  # select_library_configurations; target_link_libraries resolves them.
  target_link_libraries(PkgConfig::FFMPEG INTERFACE ${FFMPEG_LIBRARIES})
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale)
endif()

# Repository-pinned header dependencies. No configure-time downloads.
add_library(ninfer::json INTERFACE IMPORTED GLOBAL)
target_include_directories(ninfer::json INTERFACE
  ${PROJECT_SOURCE_DIR}/third_party)

# Source base for the custom-template frontend; consumers will link it explicitly.
add_subdirectory(third_party/llama-jinja EXCLUDE_FROM_ALL)

if(NINFER_BUILD_PRODUCT_SUPPORT)
  # Media acquisition uses CURLOPT_PROTOCOLS_STR and CURLOPT_REDIR_PROTOCOLS_STR,
  # introduced in libcurl 7.85 (not merely the version of the maintainer environment).
  if(WIN32)
    # vcpkg builds curl against Schannel; see vcpkg.json.
    find_package(CURL REQUIRED)
    message(STATUS "libcurl: ${CURL_VERSION_STRING}")
    if(CURL_VERSION_STRING VERSION_LESS 7.85)
      message(FATAL_ERROR
        "NInfer requires libcurl >= 7.85 for CURLOPT_PROTOCOLS_STR; got ${CURL_VERSION_STRING}")
    endif()
    add_library(PkgConfig::LIBCURL INTERFACE IMPORTED)
    set_target_properties(PkgConfig::LIBCURL PROPERTIES
      INTERFACE_LINK_LIBRARIES CURL::libcurl)
  else()
    pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl>=7.85)
  endif()
  add_library(ninfer::httplib INTERFACE IMPORTED GLOBAL)
  target_include_directories(ninfer::httplib INTERFACE
    ${PROJECT_SOURCE_DIR}/third_party/cpp-httplib)
  add_subdirectory(third_party/spdlog)
endif()
