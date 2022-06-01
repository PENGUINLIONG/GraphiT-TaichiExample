cmake_minimum_required (VERSION 3.10)

# Replace this with your project name.
set(PROJECT_NAME "GraphiT-Template")

project (${PROJECT_NAME} LANGUAGES CXX)



option(WITH_VULKAN "Build Graphi-T with Vulkan GPU backend" ON)
option(WITH_GLSLANG "Build Graphi-T with glslang for runtime-shader compilation" ON)



set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")



set(GFT_BUILD_APPS "Build Graphi-T example apps" OFF)

if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release" CACHE PATH "Build Type" FORCE)
endif()

set(GFT_WITH_VULKAN ${WITH_VULKAN})
set(GFT_WITH_GLSLANG ${WITH_GLSLANG})

add_definitions(-DGFT_WITH_VULKAN=${WITH_VULKAN})
add_definitions(-DGFT_WITH_GLSLANG=${WITH_GLSLANG})

add_subdirectory(third/graphi-t)

find_package(Vulkan REQUIRED)

set(LINK_LIBS
    ${Vulkan_LIBRARIES}
    GraphiT
)
set(INC_DIRS
    ${Vulkan_INCLUDE_DIRS}
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third/graphi-t/include"
)

# The GraphiT library.
include_directories(${INC_DIRS})
file(GLOB_RECURSE SRCS "${PROJECT_SOURCE_DIR}/src/*")
file(GLOB_RECURSE INCS "${PROJECT_SOURCE_DIR}/include/*")
add_executable(${PROJECT_NAME} ${SRCS} ${INCS})
target_link_libraries(${PROJECT_NAME} GraphiT)
