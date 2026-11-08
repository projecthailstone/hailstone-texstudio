cmake_minimum_required(VERSION 3.25)
# Hailstone Texture Studio: HD texture packs built from the player's own game files on their graphics card, a tool
# add-on of Hailstone (the client's docs/addons.md "Tools"). It builds against a hailstone-client checkout
# (HAILSTONE_CLIENT_DIR): the archive readers, the texture pack rules (src/texpack), the Lua UI, Vulkan and ImGui
# layers are the client's; the upscaler (ncnn) and the window are here.
project(hailstone-texstudio VERSION 1.0.0 LANGUAGES C CXX)
set(TEXSTUDIO_VERSION_LABEL "beta")
if(TEXSTUDIO_VERSION_LABEL STREQUAL "")
  set(TEXSTUDIO_VERSION ${PROJECT_VERSION})
else()
  set(TEXSTUDIO_VERSION ${PROJECT_VERSION}-${TEXSTUDIO_VERSION_LABEL})
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# As the client (its CMakeLists.txt): Lua errors longjmp through C++ frames, which MSVC unwinds exactly only under /EHs.
if(MSVC)
  string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# ---- the client
set(HAILSTONE_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../hailstone-client" CACHE PATH "A hailstone-client checkout")
if(NOT EXISTS "${HAILSTONE_CLIENT_DIR}/src/texpack/texpack.h")
  message(FATAL_ERROR "HAILSTONE_CLIENT_DIR (${HAILSTONE_CLIENT_DIR}) is not a hailstone-client checkout with src/texpack")
endif()
set(HAILSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HAILSTONE_BROWSER OFF CACHE BOOL "" FORCE)
# Only what the studio links is built (hailstone_engine, hailstone_texpack and their shaders); not the client or its tools.
add_subdirectory(${HAILSTONE_CLIENT_DIR} hailstone EXCLUDE_FROM_ALL)
set(HAILSTONE_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/hailstone)

if(MSVC)
  set(TEXSTUDIO_WARNINGS /W4 /permissive- /utf-8)
else()
  set(TEXSTUDIO_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
                         -Wno-missing-field-initializers)
endif()

# ---- ncnn (BSD-3-Clause): the models on the GPU through Vulkan compute (its own loader: nothing else to install), or
# the CPU.
include(FetchContent)
set(NCNN_VULKAN ON CACHE BOOL "" FORCE)
set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
set(NCNN_PIXEL_DRAWING OFF CACHE BOOL "" FORCE)
set(NCNN_DISABLE_EXCEPTION OFF CACHE BOOL "" FORCE)
# OpenMP: the CPU path's threads, GCC's libgomp on Linux. Off on Windows: MSVC's would need vcomp140.dll, and ncnn's
# simple OpenMP needs pthreads. The GPU path does not use it; only the CPU fallback runs on one thread there.
if(MSVC)
  set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
  # No AVX-512, AVX-NE-CONVERT, AVX-VNNI, FMA4 or XOP copies of ncnn's layers (SSE, AVX, FMA and AVX2 stay): with
  # them ncnn.lib's lib command is 44,700 characters, over the 32,767 a Windows command line holds, and MSVC under
  # Wine hangs on it (CMake's Ninja generator takes no response file on a Linux host, and msvc-wine does not rewrite
  # the paths in a compile's). Without them it is about 29,500. Only the CPU fallback runs them.
  foreach(isa AVX512 AVX512VNNI AVX512BF16 AVX512FP16 AVXNECONVERT AVXVNNI AVXVNNIINT8 AVXVNNIINT16 FMA4 XOP)
    set(NCNN_${isa} OFF CACHE BOOL "" FORCE)
  endforeach()
endif()
FetchContent_Declare(ncnn
  GIT_REPOSITORY https://github.com/Tencent/ncnn.git
  GIT_TAG 7e36d25cb153092ba87a60530455c7376fedf452 # 20261020
  GIT_SHALLOW FALSE
  GIT_SUBMODULES glslang
  SYSTEM)
FetchContent_MakeAvailable(ncnn)

# ---- the studio
add_library(texstudio_ncnn STATIC src/ncnn_model.cpp)
target_compile_options(texstudio_ncnn PRIVATE ${TEXSTUDIO_WARNINGS})
target_include_directories(texstudio_ncnn PUBLIC src)
target_link_libraries(texstudio_ncnn PUBLIC hailstone_texpack ncnn)

# The add-on's folder: dist/ holds what tools/<name>/ will (the program, its SDL, shaders, its window's Lua with the
# modern pack's lib/, the icons and pictures, the models). tools/release.sh packs it.
set(TEXSTUDIO_DIST ${CMAKE_CURRENT_BINARY_DIR}/dist)
set(TEXSTUDIO_WIN_SOURCES "")
if(MSVC)
  set(TEXSTUDIO_WIN_SOURCES ${HAILSTONE_CLIENT_DIR}/cmake/utf8.manifest ${HAILSTONE_CLIENT_DIR}/cmake/windows/hailstone.rc)
endif()
add_executable(texstudio src/main.cpp src/studio.cpp src/studio_lua.cpp ${TEXSTUDIO_WIN_SOURCES})
target_compile_options(texstudio PRIVATE ${TEXSTUDIO_WARNINGS})
target_compile_definitions(texstudio PRIVATE TEXSTUDIO_VERSION="${TEXSTUDIO_VERSION}")
target_link_libraries(texstudio PRIVATE hailstone_engine texstudio_ncnn)
set_target_properties(texstudio PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TEXSTUDIO_DIST}$<0:>)
if(MSVC)
  # A Release build is a GUI program: no console beside it (as the client).
  target_link_options(texstudio PRIVATE $<$<CONFIG:Release,MinSizeRel>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>)
endif()
if(APPLE)
  set_target_properties(texstudio PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
elseif(UNIX)
  set_target_properties(texstudio PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

# Everything it reads from its own folder, copied beside it at each build.
set(client_modern ${HAILSTONE_CLIENT_DIR}/ui/modern)
add_custom_command(TARGET texstudio POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${TEXSTUDIO_DIST}/shaders ${TEXSTUDIO_DIST}/models
          ${TEXSTUDIO_DIST}/ui/modern/tools
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${HAILSTONE_BUILD_DIR}/shaders ${TEXSTUDIO_DIST}/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${client_modern}/lib ${TEXSTUDIO_DIST}/ui/modern/lib
  COMMAND ${CMAKE_COMMAND} -E copy ${client_modern}/ui.ini ${TEXSTUDIO_DIST}/ui/modern/ui.ini
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/ui/modern/tools/texstudio.lua
          ${TEXSTUDIO_DIST}/ui/modern/tools/texstudio.lua
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${HAILSTONE_CLIENT_DIR}/icons ${TEXSTUDIO_DIST}/icons
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${HAILSTONE_CLIENT_DIR}/pictures ${TEXSTUDIO_DIST}/pictures
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/models ${TEXSTUDIO_DIST}/models
  VERBATIM)
# The shared libraries it was linked with that the build made (SDL3 when it was fetched): Windows has no rpath, and
# on Linux the $ORIGIN rpath finds them beside it.
if(WIN32)
  add_custom_command(TARGET texstudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:texstudio> ${TEXSTUDIO_DIST}
    COMMAND_EXPAND_LISTS)
elseif(TARGET SDL3-shared)
  get_target_property(texstudio_sdl_imported SDL3-shared IMPORTED)
  if(NOT texstudio_sdl_imported)
    add_custom_command(TARGET texstudio POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_SONAME_FILE:SDL3-shared> ${TEXSTUDIO_DIST})
  endif()
endif()

# The models: their descriptions (models/*.ini) are here; the converted ncnn files (.param, .bin) are not in the
# repository. TEXSTUDIO_MODELS names a folder holding them (tools/convert_models.py makes it).
set(TEXSTUDIO_MODELS "" CACHE PATH "Folder of the converted ncnn models (.param/.bin)")
if(TEXSTUDIO_MODELS)
  file(GLOB texstudio_model_files ${TEXSTUDIO_MODELS}/*.param ${TEXSTUDIO_MODELS}/*.bin)
  if(texstudio_model_files)
    add_custom_command(TARGET texstudio POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${texstudio_model_files} ${TEXSTUDIO_DIST}/models)
  else()
    message(WARNING "TEXSTUDIO_MODELS (${TEXSTUDIO_MODELS}) holds no .param/.bin files")
  endif()
else()
  message(STATUS "TEXSTUDIO_MODELS is not set: the studio is built without its models")
endif()

# ---- tests: the program's own command line, against the folder it is built into.
enable_testing()
add_test(NAME texstudio_version COMMAND texstudio --version)
set_tests_properties(texstudio_version PROPERTIES PASS_REGULAR_EXPRESSION "Hailstone Texture Studio ${TEXSTUDIO_VERSION}")
add_test(NAME texstudio_data_dir COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:texstudio>
         -DWORK=${CMAKE_CURRENT_BINARY_DIR}/test-data -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/data_dir.cmake)
