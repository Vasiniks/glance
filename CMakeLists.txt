cmake_minimum_required(VERSION 3.20)
project(Glance VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lightweight defaults — no SDK pin, no CMake pin.
if(NOT WIN32)
  message(FATAL_ERROR "Glance is Windows-only")
endif()

# Single executable, no static libs for minimal link overhead.
add_executable(glance WIN32
  src/main.cpp
  src/telemetry.cpp
  src/renderer.cpp
  src/appbar.cpp
  src/config.cpp
)

set_target_properties(glance PROPERTIES OUTPUT_NAME Glance)

target_include_directories(glance PRIVATE src)

# Only what we actually use.
target_link_libraries(glance PRIVATE
  user32 gdi32 shell32 shcore pdh iphlpapi
)

# /O1 and size opts when GLANCE_TINY=ON
option(GLANCE_TINY "Optimize for smallest binary" OFF)

target_compile_definitions(glance PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  WINVER=0x0A00 _WIN32_WINNT=0x0A00
)

if(MSVC)
  target_compile_options(glance PRIVATE /W4 /permissive- /utf-8 /EHsc /GR-)
  target_link_options(glance PRIVATE /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /INCREMENTAL:NO /OPT:REF /OPT:ICF)
  if(GLANCE_TINY)
    target_compile_options(glance PRIVATE /O1 /Os /GL)
    target_link_options(glance PRIVATE /LTCG /DEBUG:NONE)
  else()
    target_compile_options(glance PRIVATE $<$<CONFIG:Release>:/O2>)
  endif()
endif()

# Optional icon resource — add resources/glance.rc if you have an .ico

enable_testing()
add_executable(glance_tests tests/tests.cpp src/telemetry.cpp src/config.cpp)
target_include_directories(glance_tests PRIVATE src)
target_link_libraries(glance_tests PRIVATE pdh iphlpapi user32 gdi32 shcore shell32)
set_target_properties(glance_tests PROPERTIES OUTPUT_NAME glance_tests)
add_test(NAME unit COMMAND glance_tests)
set_tests_properties(unit PROPERTIES TIMEOUT 30)
