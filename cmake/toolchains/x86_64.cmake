set(CMAKE_SYSTEM_NAME Anillo)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program(CLANG_PATH clang REQUIRED)
find_program(CLANGXX_PATH clang++ REQUIRED)
find_program(REAL_LD_PATH x86_64-apple-darwin11-ld REQUIRED)
find_program(LLVM_AR_PATH llvm-ar REQUIRED)
find_program(LLVM_NM_PATH llvm-nm REQUIRED)
find_program(LLVM_RANLIB_PATH llvm-ranlib REQUIRED)
find_program(LLVM_SIZE_PATH llvm-size REQUIRED)
find_program(LLVM_STRIP_PATH llvm-strip REQUIRED)
find_program(LLVM_OBJCOPY_PATH llvm-objcopy REQUIRED)

configure_file("${CMAKE_SOURCE_DIR}/cmake/toolchains/ld-wrapper.sh.in" "${CMAKE_BINARY_DIR}/ld-wrapper.sh" USE_SOURCE_PERMISSIONS @ONLY)

set(LD_PATH "${CMAKE_BINARY_DIR}/ld-wrapper.sh" CACHE STRING "")

set(CMAKE_AR "${LLVM_AR_PATH}" CACHE INTERNAL "")
set(CMAKE_ASM_COMPILER "${CLANG_PATH}" CACHE INTERNAL "")
set(CMAKE_C_COMPILER "${CLANG_PATH}" CACHE INTERNAL "")
set(CMAKE_CXX_COMPILER "${CLANGXX_PATH}" CACHE INTERNAL "")
set(CMAKE_LINKER "${LD_PATH}" CACHE INTERNAL "")
set(CMAKE_OBJCOPY "${LLVM_OBJCOPY_PATH}" CACHE INTERNAL "")
set(CMAKE_RANLIB "${LLVM_RANLIB_PATH}" CACHE INTERNAL "")
set(CMAKE_SIZE "${LLVM_SIZE_PATH}" CACHE INTERNAL "")
set(CMAKE_STRIP "${LLVM_STRIP_PATH}" CACHE INTERNAL "")

set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${LD_PATH} -Wl,-platform_version,macos,10.15,10.15 -Wl,-Z -Wl,-arch,x86_64")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "--ld-path=${LD_PATH} -Wl,-platform_version,macos,10.15,10.15 -Wl,-Z -Wl,-arch,x86_64")

set(ANILLO_TARGET_TRIPLE x86_64-unknown-none-macho)

# TODO: get rid of `-mno-implicit-float`
set(CMAKE_C_FLAGS "-ffreestanding -mno-implicit-float -nostdlib -target ${ANILLO_TARGET_TRIPLE} -U__APPLE__ -D__ANILLO__=1 -mmacosx-version-min=10.15" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}" CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS "-nostdlib -target ${ANILLO_TARGET_TRIPLE}" CACHE INTERNAL "")

set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -gfull" CACHE INTERNAL "")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG=1" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" CACHE INTERNAL "")

set(CMAKE_C_COMPILER_FORCED TRUE CACHE INTERNAL "")
set(CMAKE_CXX_COMPILER_FORCED TRUE CACHE INTERNAL "")

set(CMAKE_SIZEOF_VOID_P 8 CACHE INTERNAL "")
