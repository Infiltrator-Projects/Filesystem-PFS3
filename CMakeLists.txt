# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILESYSTEM_VERSION LIMIT_COUNT 1)
project(FilesystemPFS3 VERSION ${FILESYSTEM_VERSION} LANGUAGES C)
include(CTest)
include(FetchContent)
find_package(OpenSSL REQUIRED)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(INFILTRATR_COMMON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INFILTRATR_COMMON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(infiltratr_common
  GIT_REPOSITORY https://github.com/Infiltrator-Projects/Libraries.git
  GIT_TAG d23a52693fd3212368d327e1c1302e07ae9510ec)
FetchContent_MakeAvailable(infiltratr_common)
set_property(DIRECTORY "${infiltratr_common_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h"
  "#define LD_VERSION \"${PROJECT_VERSION}\"\n")

# Preserve the canonical PFS3 native format code, not the historical V1
# worker/service shim, in the installed Common V2 provider.
add_library(pfs3-driver SHARED
  module/pfs3_module.c
  native/filesystems/pfs3/core/pfs3_core.c
  native/filesystems/pfs3/core/pfs3_reader_v2.c)
set_target_properties(pfs3-driver PROPERTIES
  PREFIX "" OUTPUT_NAME "PFS3" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_options(pfs3-driver PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(pfs3-driver PRIVATE
  -Wl,-z,defs
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module/v2-only.map")
target_include_directories(pfs3-driver PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/pfs3/core"
  "${infiltratr_common_SOURCE_DIR}/include")
target_link_libraries(pfs3-driver PRIVATE InfiltratrCommon::Common)


# The same format-owned C code is compiled as a no-import UEFI reader and
# embedded in the one installed Common V2 shared object. Validation and OVMF
# qualification are separate release gates, not assumed by compilation.
set(PFS3_FIRMWARE_DIR "${CMAKE_CURRENT_BINARY_DIR}/firmware")
set(PFS3_FIRMWARE_ELF "${PFS3_FIRMWARE_DIR}/PFS3.so")
add_custom_command(OUTPUT "${PFS3_FIRMWARE_ELF}"
 COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}/module/firmware"
   -B "${PFS3_FIRMWARE_DIR}" -DCMAKE_BUILD_TYPE=Release
   "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
   "-DCOMMON_INCLUDE_DIR=${infiltratr_common_SOURCE_DIR}/include"
 COMMAND "${CMAKE_COMMAND}" --build "${PFS3_FIRMWARE_DIR}" --parallel 2
 DEPENDS module/firmware/CMakeLists.txt module/firmware/runtime.c
   module/pfs3_module.c
   native/filesystems/pfs3/core/pfs3_core.c
   native/filesystems/pfs3/core/pfs3_reader_v2.c
   native/filesystems/pfs3/core/pfs3_reader_v2.h
 VERBATIM)
add_custom_target(pfs3-firmware-elf DEPENDS "${PFS3_FIRMWARE_ELF}")
if(NOT CMAKE_OBJCOPY)
 find_program(CMAKE_OBJCOPY NAMES objcopy llvm-objcopy REQUIRED)
endif()
add_dependencies(pfs3-driver pfs3-firmware-elf)
set_property(TARGET pfs3-driver APPEND PROPERTY LINK_DEPENDS "${PFS3_FIRMWARE_ELF}")
add_custom_command(TARGET pfs3-driver POST_BUILD
 COMMAND "${CMAKE_OBJCOPY}" --add-section
   ".infiltrator_boot=${PFS3_FIRMWARE_ELF}"
   --set-section-flags .infiltrator_boot=readonly
   "$<TARGET_FILE:pfs3-driver>"
 VERBATIM)


if(BUILD_TESTING)
  # Keep the historical worker/fixture code for independent regression only;
  # no legacy V1 ABI is installed with the production PFS3.so.
  add_library(pfs3-historical-test-support SHARED
    native/pfs3_native.c
    native/pfs3_worker.c
    module/pfs3_module.c
    compat/legacy_services.c
    native/filesystems/pfs3/core/pfs3_core.c
    fixtures/pfs3_codec.c
    compat/fixture_bridge.c)
  set_source_files_properties(native/pfs3_worker.c PROPERTIES
    COMPILE_DEFINITIONS "main=ifs_pfs3_worker_main")
  target_compile_definitions(pfs3-historical-test-support PRIVATE
    _GNU_SOURCE _FILE_OFFSET_BITS=64)
  target_compile_options(pfs3-historical-test-support PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_include_directories(pfs3-historical-test-support PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/pfs3/core"
    "${infiltratr_common_SOURCE_DIR}/include")
  target_link_libraries(pfs3-historical-test-support PRIVATE
    InfiltratrCommon::Common OpenSSL::Crypto)

  add_executable(pfs3-module-test tests/test_pfs3_module.c)
  target_include_directories(pfs3-module-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/pfs3/core"
    "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_options(pfs3-module-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(pfs3-module-test PRIVATE
    pfs3-driver InfiltratrCommon::Common)
  add_test(NAME pfs3-v2-multivolume-probe-mount COMMAND pfs3-module-test)

  # Qualify the source-owned direct-index reader before production V2
  # advertises namespace callbacks; never promote on a compile-only pass.
  add_executable(pfs3-native-reader-test
    tests/test_pfs3_reader_v2.c
    native/filesystems/pfs3/core/pfs3_reader_v2.c
    native/filesystems/pfs3/core/pfs3_core.c)
  target_include_directories(pfs3-native-reader-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/pfs3/core"
    "${infiltratr_common_SOURCE_DIR}/include")
  target_compile_options(pfs3-native-reader-test PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(pfs3-native-reader-test PRIVATE
    InfiltratrCommon::Common)
  add_test(NAME pfs3-real-direct-index-v2-reader COMMAND pfs3-native-reader-test)

  # Exercise the native disk decoder without exporting its private symbols.
  add_executable(pfs3-canonical-core-link-test
    tests/test_canonical_core.c
    native/filesystems/pfs3/core/pfs3_core.c)
  target_include_directories(pfs3-canonical-core-link-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/native/filesystems/pfs3/core")
  target_compile_options(pfs3-canonical-core-link-test PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME pfs3-canonical-core-link COMMAND pfs3-canonical-core-link-test)
  add_test(NAME pfs3-only-v2-dynamic-exports
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_v2_exports.sh"
      "$<TARGET_FILE:pfs3-driver>")
endif()
install(TARGETS pfs3-driver LIBRARY DESTINATION /System/Filesystems)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/PFS3.pdf"
  DESTINATION /System/Resources/Documentation RENAME Filesystem-PFS3.pdf)
