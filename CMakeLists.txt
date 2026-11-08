cmake_minimum_required(VERSION 3.20)
project(Glance VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "Glance is Windows-only")
endif()

# Single exe — no static libs for minimal overhead
add_executable(glance WIN32
  src/main.cpp
  src/telemetry.cpp
  src/renderer.cpp
  src/appbar.cpp
  src/config.cpp
)
set_target_properties(glance PROPERTIES OUTPUT_NAME Glance)
target_include_directories(glance PRIVATE src)
target_link_libraries(glance PRIVATE user32 gdi32 shell32 shcore pdh)

option(GLANCE_TINY "Optimize for smallest binary" OFF)

target_compile_definitions(glance PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  WINVER=0x0A00 _WIN32_WINNT=0x0A00
)

if(MSVC)
  # Lightest: no RTTI, no EH exception tables where possible, /O1 /Os
  target_compile_options(glance PRIVATE
    /W4 /permissive- /utf-8
    /EHsc /GR-                 # no RTTI
    /Zc:inline /GF /Gy         # merge strings + func-level linking
  )
  target_link_options(glance PRIVATE
    /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
    /INCREMENTAL:NO
    /OPT:REF /OPT:ICF
    /SUBSYSTEM:WINDOWS
  )
  if(GLANCE_TINY)
    target_compile_options(glance PRIVATE /O1 /Os /GL)
    target_link_options(glance PRIVATE /LTCG /DEBUG:NONE)
    # Further size wins (optional, not default because VS version dependent):
    # /MERGE:.rdata=.text would pack but leave wx protection — leave out by default
  else()
    target_compile_options(glance PRIVATE /O2)
    target_link_options(glance PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL>)
  endif()
endif()

# Tests — also lightweight, no iostream
add_executable(glance_tests tests/tests.cpp src/config.cpp)
target_include_directories(glance_tests PRIVATE src)
target_link_libraries(glance_tests PRIVATE user32 shell32 shcore)
set_target_properties(glance_tests PROPERTIES OUTPUT_NAME glance_tests)

enable_testing()
add_test(NAME unit COMMAND glance_tests)
set_tests_properties(unit PROPERTIES TIMEOUT 10)
