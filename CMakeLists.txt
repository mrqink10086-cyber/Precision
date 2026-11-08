cmake_minimum_required(VERSION 3.22)

project(
	Precision
	VERSION 2.0.6
	LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
	if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif()
endmacro()

#set_from_environment(CompiledPluginsPath)
#if(NOT DEFINED CompiledPluginsPath)
#	message(FATAL_ERROR "CompiledPluginsPath is not set")
#endif()

option(COPY_OUTPUT "Copy the output of build operations to the game directory" OFF)
option(ENABLE_SKYRIM_SE "Enable support for Skyrim SE in the dynamic runtime feature." ON)
option(ENABLE_SKYRIM_AE "Enable support for Skyrim AE in the dynamic runtime feature." ON)
option(ENABLE_SKYRIM_VR "Enable support for Skyrim VR in the dynamic runtime feature." OFF)
set(BUILD_TESTS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

add_subdirectory(src)
include(cmake/packaging.cmake)

option(PRECISION_BUILD_TESTS "Build standalone runtime algorithm tests" OFF)
if(PRECISION_BUILD_TESTS)
	enable_testing()
	add_executable(precision_runtime_tests tests/runtime_tests.cpp)
	target_compile_features(precision_runtime_tests PRIVATE cxx_std_20)
	target_include_directories(precision_runtime_tests PRIVATE src)
	add_test(NAME precision_runtime_tests COMMAND precision_runtime_tests)
	set_tests_properties(precision_runtime_tests PROPERTIES TIMEOUT 30)

	add_executable(precision_runtime_bench EXCLUDE_FROM_ALL tests/runtime_bench.cpp)
	target_compile_features(precision_runtime_bench PRIVATE cxx_std_20)
	target_include_directories(precision_runtime_bench PRIVATE src)
endif()
