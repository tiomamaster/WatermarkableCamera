cmake_minimum_required(VERSION 3.18.1)
project(WatermarkableCameraJNI)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

# download CPM.cmake
file(
        DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.0/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

include(AndroidNdkModules)

# Include the GameActivity static lib to the project.
find_package(game-activity REQUIRED CONFIG)
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u \
    Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

find_package(VulkanHpp REQUIRED)
cpmaddpackage("gh:g-truc/glm#1.0.2")
cpmaddpackage(NAME stb URL
        https://github.com/nothings/stb/archive/refs/heads/master.tar.gz)

# Now build app's shared lib
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall")

add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR=1)

find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

function(add_slang_shader_target TARGET)
    cmake_parse_arguments("SHADER" "" "" "SOURCES" ${ARGN})
    set(SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
    set(SHADERS_OUT_DIR "${CMAKE_CURRENT_LIST_DIR}/../assets/shaders")
    set(ENTRY_POINTS -entry vertMain -entry fragMain)
    add_custom_command(OUTPUT ${SHADERS_OUT_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_OUT_DIR})
    add_custom_command(
            OUTPUT ${SHADERS_OUT_DIR}/tex.spv
            COMMAND
            ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_3
            -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o tex.spv
            WORKING_DIRECTORY ${SHADERS_OUT_DIR}
            DEPENDS ${SHADERS_OUT_DIR} ${SHADER_SOURCES}
            COMMENT "Compiling Slang Shaders"
            VERBATIM)
    add_custom_target(${TARGET} DEPENDS ${SHADERS_OUT_DIR}/tex.spv)
endfunction()

add_library(${PROJECT_NAME} SHARED main.cpp camera_utils.cpp)

# add lib dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC
        game-activity::game-activity_static
        android
        log
        m
        camera2ndk
        mediandk
        vulkan
        VulkanHpp::VulkanHpp
        glm)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/_deps/stb-src)
add_slang_shader_target(main_slang_shader SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/shaders/tex.slang)
add_dependencies(${PROJECT_NAME} main_slang_shader)

target_compile_definitions(${PROJECT_NAME}
        PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1)

target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=16384")