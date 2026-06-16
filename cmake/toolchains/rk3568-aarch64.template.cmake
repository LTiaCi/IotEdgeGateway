# Copy this file to rk3568-aarch64.cmake and edit the paths for your VM.
#
# Example configure command:
#   cmake -S . -B build-rk3568 \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-aarch64.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Set these two paths to the ARM64 compiler in your rk356x_linux SDK.
set(CMAKE_C_COMPILER /absolute/path/to/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /absolute/path/to/aarch64-linux-gnu-g++)

# Recommended when your SDK provides a board-matched sysroot.
# set(CMAKE_SYSROOT /absolute/path/to/rk356x_linux/sysroot)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
