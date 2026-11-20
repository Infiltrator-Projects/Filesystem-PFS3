# SPDX-License-Identifier: GPL-3.0-or-later
# No duplicated format implementation, V1, glibc or Linux driver.
cmake_minimum_required(VERSION 3.20)
project(PFS3FirmwareCommonV2 LANGUAGES C)
set(COMMON_INCLUDE_DIR "" CACHE PATH "Pinned Common V2 include")
if(NOT EXISTS "${COMMON_INCLUDE_DIR}/infiltratr/filesystem_module_v2.h")
  message(FATAL_ERROR "Pinned Common V2 header required")
endif()
add_library(pfs3-firmware MODULE
  ../../module/pfs3_module.c
  ../../native/filesystems/pfs3/core/pfs3_core.c
  ../../native/filesystems/pfs3/core/pfs3_reader_v2.c
  runtime.c)
set_target_properties(pfs3-firmware PROPERTIES
  PREFIX "" OUTPUT_NAME "PFS3" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_include_directories(pfs3-firmware PRIVATE
  "${COMMON_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../native/filesystems/pfs3/core")
target_compile_features(pfs3-firmware PRIVATE c_std_11)
target_compile_definitions(pfs3-firmware PRIVATE IFS_PFS3_FIRMWARE_BUILD=1)
target_compile_options(pfs3-firmware PRIVATE
  -O2 -Wall -Wextra -Wpedantic -Werror
  -ffreestanding -fno-builtin -fno-stack-protector
  -ffunction-sections -fdata-sections -mno-red-zone
  -fno-tree-loop-distribute-patterns -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_link_options(pfs3-firmware PRIVATE
  -nostdlib -Wl,-z,defs,-z,noexecstack,-z,relro,-z,now,--gc-sections,-Bsymbolic)
