cmake_minimum_required(VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("IPoverObexPC")

# -------------------------------------------------------------------
# Global C++ settings
# -------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS FALSE)

# -------------------------------------------------------------------
# sdbus-c++
# -------------------------------------------------------------------

if(UNIX)
  include(FetchContent)

  FetchContent_Declare(
    sdbus-c++
    GIT_REPOSITORY https://github.com/Kistler-Group/sdbus-cpp.git
    GIT_TAG master
  )

  set(BUILD_EXAMPLES OFF)

  FetchContent_MakeAvailable(sdbus-c++)
endif()

# -------------------------------------------------------------------
# Output directory
# -------------------------------------------------------------------

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# -------------------------------------------------------------------
# SFML
# -------------------------------------------------------------------

set(SFML_BUILD_AUDIO FALSE)
set(SFML_BUILD_GRAPHICS FALSE)
set(SFML_BUILD_WINDOW FALSE)

add_subdirectory(SFML)

# -------------------------------------------------------------------
# Project modules
# -------------------------------------------------------------------

add_subdirectory(main)
add_subdirectory(DataStream)
add_subdirectory(BT)
add_subdirectory(OBEX)
add_subdirectory(StreamToIP)

# -------------------------------------------------------------------
# Compiler options
# -------------------------------------------------------------------

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

  target_compile_options(sdbus-c++-objlib PRIVATE -fcoroutines)

  target_compile_options(StreamToIP PRIVATE -fcoroutines)
  target_compile_options(BT PRIVATE -fcoroutines)
  target_compile_options(DataStream PRIVATE -fcoroutines)
  target_compile_options(main PRIVATE -fcoroutines)

endif()

# -------------------------------------------------------------------
# Enforce C++20 per target
# -------------------------------------------------------------------

target_compile_features(sdbus-c++-objlib PRIVATE cxx_std_20)

target_compile_features(StreamToIP PRIVATE cxx_std_20)
target_compile_features(BT PRIVATE cxx_std_20)
target_compile_features(DataStream PRIVATE cxx_std_20)
target_compile_features(main PRIVATE cxx_std_20)


