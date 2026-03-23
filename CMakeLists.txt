cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _version)
string(STRIP "${_version}" _version)
# CMake VERSION only accepts numeric major.minor.patch; strip pre-release suffix
# (e.g. "0.0.1-alpha.1" -> "0.0.1")
string(REGEX REPLACE "[-+].*$" "" _cmake_version "${_version}")

project(maintenance_state_store VERSION ${_cmake_version} LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Default BUILD_TESTING to OFF when used as a subdirectory (e.g. FetchContent)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    option(BUILD_TESTING "Build GoogleTest unit tests" ON)
else()
    option(BUILD_TESTING "Build GoogleTest unit tests" OFF)
endif()

# ---------------------------------------------------------------------------
# FetchContent dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# nlohmann/json — prefer an already-provided version (e.g. from parent project)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# GoogleTest (only fetched when testing is requested)
if(BUILD_TESTING)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    # Prevent GTest from overriding compiler/linker options
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # Do not install GTest/GMock as part of this project's install target
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# C++ libraries: static (.a) and shared (.so)
# ---------------------------------------------------------------------------
foreach(_type STATIC SHARED)
    if(_type STREQUAL "STATIC")
        set(_target maintenance_state_store)
    else()
        set(_target maintenance_state_store_shared)
    endif()

    add_library(${_target} ${_type}
        src/maintenance_state_store.cpp
    )

    set_target_properties(${_target} PROPERTIES
        OUTPUT_NAME maintenance_state_store
        VERSION     ${PROJECT_VERSION}
        SOVERSION   ${PROJECT_VERSION_MAJOR}
    )

    target_compile_features(${_target} PUBLIC cxx_std_17)

    target_include_directories(${_target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(${_target}
        PRIVATE
            nlohmann_json::nlohmann_json
    )
endforeach()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS maintenance_state_store maintenance_state_store_shared
    EXPORT maintenance_state_store_targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT maintenance_state_store_targets
    FILE maintenance_state_store_targets.cmake
    NAMESPACE maintenance::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/maintenance_state_store
)

# Generate and install package config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/maintenance_state_store-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/maintenance_state_store-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/maintenance_state_store
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/maintenance_state_store-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/maintenance_state_store-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/maintenance_state_store-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/maintenance_state_store
)

# ---------------------------------------------------------------------------
# ament_cmake support (ROS2 / colcon workspace)
# ---------------------------------------------------------------------------
find_package(ament_cmake QUIET)
if(ament_cmake_FOUND)
    ament_export_targets(maintenance_state_store_targets HAS_LIBRARY_TARGET)
    ament_export_include_directories(include)
    ament_package()
endif()
