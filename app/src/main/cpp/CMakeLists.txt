cmake_minimum_required(VERSION 3.18.1)
project(WatermarkableCameraJNI)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

# Include the GameActivity static lib to the project.
find_package(game-activity REQUIRED CONFIG)
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u \
    Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

find_package(VulkanHpp REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wall")

add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR=1)

add_library(${PROJECT_NAME} SHARED main.cpp)

# add lib dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC
        vulkan
        VulkanHpp::VulkanHpp
        game-activity::game-activity_static
        android
        log)
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=16384")