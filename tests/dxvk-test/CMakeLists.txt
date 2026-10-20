# SSBU v00.00: the DXVK test app (main.cpp). Configured by scripts/build-dxvk-test.sh with toolchain/ps4-ssbu.cmake
# and toolchain/ps4-love-style.cmake as the project include (PS4_RUNTIME_SOURCES, ps4_link_mesa, ps4_add_eboot).
cmake_minimum_required(VERSION 3.20)
project(ssbu_dxvk_test C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(PORT ${CMAKE_CURRENT_SOURCE_DIR}/../../port)
set(DXVK_SRC "$ENV{HOME}/ssbu-build/src/dxvk" CACHE PATH "DXVK source (branch ps4)")
set(DXVK_BUILD "$ENV{HOME}/ssbu-build/build/dxvk" CACHE PATH "DXVK PS4 build (scripts/build-dxvk.sh)")

add_executable(eboot main.cpp ${PORT}/ps4_vk_wsi.cpp ${PS4_RUNTIME_SOURCES})
target_include_directories(eboot PRIVATE
    ${DXVK_SRC}/include/native ${DXVK_SRC}/include/native/windows ${DXVK_SRC}/include/native/directx
    ${DXVK_SRC}/include/vulkan/include)
target_compile_options(eboot PRIVATE -fms-extensions -Wno-everything)

# DXVK's archives, in dependency order (ps4.cmake's link line is one --start-group ... --end-group already)
set(DXVK_LIBS
    src/d3d11/libdxvk_d3d11.a src/dxgi/libdxvk_dxgi.a src/dxvk/libdxvk.a src/wsi/libwsi.a
    src/vulkan/libvkcommon.a src/spirv/libspirv.a src/util/libutil.a
    subprojects/dxbc-spirv/libdxbc_spv.a subprojects/libdisplay-info/libdisplay-info.a)
list(TRANSFORM DXVK_LIBS PREPEND "${DXVK_BUILD}/")
target_link_libraries(eboot PRIVATE ${DXVK_LIBS})
ps4_link_mesa(eboot)
target_link_libraries(eboot PRIVATE ${PS4_SYSROOT}/lib/libnfs.a ${PS4_SYSROOT}/lib/libsmb2.a ${PS4_SYSROOT}/lib/libz.a
    SceSysmodule SceSystemService SceUserService SceVideoOut SceAudioOut ScePad SceNet)
ps4_add_eboot(eboot)
