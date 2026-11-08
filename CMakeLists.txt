cmake_minimum_required(VERSION 3.20)
project(Glance VERSION 0.1.0 LANGUAGES CXX RC)
# Windows only. Release binaries come from build.sh (llvm-mingw); this file is for MSVC / Visual Studio.

add_executable(Glance WIN32 src/glance.cpp res/version.rc)
target_link_libraries(Glance PRIVATE pdh comctl32 shell32 gdi32 user32 advapi32)

if(MSVC)
  # Static vcruntime + the UCRT that ships with Windows: no Visual C++ redistributable needed.
  set_property(TARGET Glance PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_sources(Glance PRIVATE res/glance.manifest)  # merged into the linker's manifest
  target_compile_options(Glance PRIVATE /W4 /O1 /GS- /GR- /EHs-c- /Gy /GF /Zc:threadSafeInit- /utf-8)
  target_link_options(Glance PRIVATE /MANIFESTUAC:NO /OPT:REF /OPT:ICF /DEBUG:NONE /NODEFAULTLIB:libucrt.lib ucrt.lib)
else()
  target_sources(Glance PRIVATE res/manifest.rc)
  target_compile_options(Glance PRIVATE -Os -fno-exceptions -fno-rtti -fno-threadsafe-statics)
  target_link_options(Glance PRIVATE -municode -static -s)
endif()
