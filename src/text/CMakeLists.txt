add_library(ninfer_text STATIC
  unicode.cpp
  ${PROJECT_SOURCE_DIR}/third_party/utf8proc/utf8proc.c
)
ninfer_internal_includes(ninfer_text)
if(WIN32)
  # utf8proc.h declares its API __declspec(dllimport) unless this is set; we link it statically.
  target_compile_definitions(ninfer_text PRIVATE UTF8PROC_STATIC)
endif()
target_include_directories(ninfer_text PRIVATE ${PROJECT_SOURCE_DIR}/third_party)

# Language evaluation reuses the engine's Unicode and JSON dependencies.
target_sources(ninfer_jinja PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/jinja.cpp)
ninfer_internal_includes(ninfer_jinja)
target_include_directories(ninfer_jinja PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(ninfer_jinja PRIVATE ninfer_text ninfer::json)
