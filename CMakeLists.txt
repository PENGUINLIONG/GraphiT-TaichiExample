cmake_minimum_required (VERSION 3.10)

# Replace this with your project name.
set(PROJECT_NAME "GraphiT-TaichiExample")

project (${PROJECT_NAME} LANGUAGES CXX)



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

add_subdirectory(third/graphi-t)

set(TI_WITH_PYTHON OFF)
set(TI_WITH_VULKAN ON)
add_subdirectory($ENV{TAICHI_REPO_DIR} taichi)
set_target_properties(taichi_c_api PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

find_package(Vulkan REQUIRED)

set(LINK_LIBS
    ${Vulkan_LIBRARIES}
    GraphiT
    taichi_c_api
)
set(INC_DIRS
    ${Vulkan_INCLUDE_DIRS}
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third/graphi-t/include"
    "$ENV{TAICHI_REPO_DIR}/c_api"
)

include_directories(${INC_DIRS})
file(GLOB_RECURSE SRCS "${PROJECT_SOURCE_DIR}/src/*")
file(GLOB_RECURSE INCS "${PROJECT_SOURCE_DIR}/include/*")
add_executable(${PROJECT_NAME} ${SRCS} ${INCS})
target_link_libraries(${PROJECT_NAME} ${LINK_LIBS})
