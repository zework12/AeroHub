cmake_minimum_required(VERSION 3.20)
project(AeroHub LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "AeroHub targets MSVC x64 only")
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)

# Shared code for the hub and for every per-game module.
add_library(AeroCore STATIC
  core/Pe.cpp
  core/Process.cpp
  core/Inject.cpp
  core/Profile.cpp
  core/Minecraft.cpp
  core/Diagnostics.cpp
  core/IatHook.cpp
  core/Ipc.cpp)
target_include_directories(AeroCore PUBLIC core)

# The application: lists game profiles, validates the running build, injects the module.
add_executable(AeroHub WIN32 hub/main.cpp hub/AeroHub.rc)
target_link_libraries(AeroHub PRIVATE AeroCore gdiplus dwmapi uxtheme shell32 ole32 user32 gdi32)
target_link_options(AeroHub PRIVATE "/MANIFESTUAC:NO")

# Forts module (loaded into Forts.exe by the hub).
add_library(FortsModule SHARED
  modules/forts/dllmain.cpp
  modules/forts/GameApi.cpp
  modules/forts/Features.cpp
  modules/forts/Menu.cpp
  modules/forts/Hud.cpp
  modules/forts/Overlay.cpp)
target_include_directories(FortsModule PRIVATE modules/forts)
target_link_libraries(FortsModule PRIVATE AeroCore opengl32 user32 gdi32 gdiplus)

# Lay out a runnable tree:  <build>/out/AeroHub.exe, out/modules/*.dll, out/profiles/*.ini
set(OUT "${CMAKE_BINARY_DIR}/out")
set_target_properties(AeroHub PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUT}")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(AeroHub PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} "${OUT}")
  set_target_properties(FortsModule PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} "${OUT}/modules")
endforeach()
set_target_properties(FortsModule PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OUT}/modules")

add_custom_command(TARGET AeroHub POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/profiles" "${OUT}/profiles"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/modules/forts/FortsModule.ini" "${OUT}/modules/FortsModule.ini")

# The Minecraft mod jar (built separately in modules/minecraft with `gradlew build`) travels with the hub, so the hub can
# install it into a Minecraft instance. Re-run CMake after building the jar for the first time.
file(GLOB AERO_MC_JARS "${CMAKE_SOURCE_DIR}/modules/minecraft/build/libs/aeroclient-*.jar")
list(FILTER AERO_MC_JARS EXCLUDE REGEX "-sources\\.jar$")
if(AERO_MC_JARS)
  list(GET AERO_MC_JARS 0 AERO_MC_JAR)
  add_custom_command(TARGET AeroHub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AERO_MC_JAR}" "${OUT}/modules/aeroclient.jar")
else()
  message(STATUS "AeroClient jar not found; run gradlew build in modules/minecraft, then re-run CMake")
endif()
