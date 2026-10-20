# The SSBU eboot: WindowSmash's runtime and the recompiled game (objects built by its tools/ps4/build_ps4.py, listed
# in <SSBU_OBJECTS>/objects.rsp), DXVK (scripts/build-dxvk.sh), RADV, and the PS4 layer (port/). Configured by
# scripts/build-ssbu.sh with toolchain/ps4-ssbu.cmake and toolchain/ps4-love-style.cmake as the project include.
cmake_minimum_required(VERSION 3.20)
project(ssbu C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(PORT ${CMAKE_CURRENT_SOURCE_DIR}/../port)
set(DXVK_SRC "$ENV{HOME}/ssbu-build/src/dxvk" CACHE PATH "DXVK source (branch ps4)")
set(DXVK_BUILD "$ENV{HOME}/ssbu-build/build/dxvk" CACHE PATH "DXVK PS4 build")
set(SSBU_OBJECTS "$ENV{HOME}/ssbu-build/build/ssbu" CACHE PATH "build_ps4.py's output")

add_executable(eboot ssbu_port.cpp ${PORT}/ps4_vk_wsi.cpp ${PS4_RUNTIME_SOURCES})
target_include_directories(eboot PRIVATE ${DXVK_SRC}/include/vulkan/include)
target_compile_options(eboot PRIVATE -Wno-everything)

# the runtime's and the game's objects (a response file: thousands of them)
target_link_options(eboot PRIVATE "@${SSBU_OBJECTS}/objects.rsp")
set_property(TARGET eboot APPEND PROPERTY LINK_DEPENDS ${SSBU_OBJECTS}/objects.rsp)

set(DXVK_LIBS
    src/d3d11/libdxvk_d3d11.a src/dxgi/libdxvk_dxgi.a src/dxvk/libdxvk.a src/wsi/libwsi.a
    src/vulkan/libvkcommon.a src/spirv/libspirv.a src/util/libutil.a
    subprojects/dxbc-spirv/libdxbc_spv.a subprojects/libdisplay-info/libdisplay-info.a)
list(TRANSFORM DXVK_LIBS PREPEND "${DXVK_BUILD}/")
target_link_libraries(eboot PRIVATE ${DXVK_LIBS} "$ENV{HOME}/ssbu-build/build/vkd3d/libvkd3d-shader.a"
    "$ENV{HOME}/ssbu-build/build/codecs/libopus.a" "$ENV{HOME}/ssbu-build/build/codecs/libvpx.a")
ps4_link_mesa(eboot)
target_link_libraries(eboot PRIVATE ${PS4_SYSROOT}/lib/libnfs.a ${PS4_SYSROOT}/lib/libsmb2.a ${PS4_SYSROOT}/lib/libz.a
    SceSysmodule SceSystemService SceUserService SceVideoOut SceAudioOut ScePad SceNet)
ps4_add_eboot(eboot)
