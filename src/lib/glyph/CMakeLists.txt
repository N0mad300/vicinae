cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME glyph)
project(${PROJECT_NAME})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

OPTION(BUILD_TESTS "Build the test suite" OFF)

set(VICINAE_DEFAULT_GLYPH_WITH_ICU ON)
if(WIN32)
	set(VICINAE_DEFAULT_GLYPH_WITH_ICU OFF)
endif()
option(VICINAE_GLYPH_WITH_ICU "Use ICU-backed emoji segmentation helpers" ${VICINAE_DEFAULT_GLYPH_WITH_ICU})

set(GLYPH_SOURCES src/glyph.cpp)

if(VICINAE_GLYPH_WITH_ICU)
	find_package(ICU REQUIRED COMPONENTS uc)
	list(APPEND GLYPH_SOURCES src/emoji.cpp)
else()
	list(APPEND GLYPH_SOURCES src/emoji-fallback.cpp)
endif()

add_library(vicinae-glyph STATIC ${GLYPH_SOURCES})
add_library(vicinae::glyph ALIAS vicinae-glyph)
set(PROJECT_NAME vicinae-glyph)
# include/glyph on the private path so the generated glyph.cpp can #include "glyph.hpp".
target_include_directories(${PROJECT_NAME} PUBLIC include PRIVATE src include/glyph)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

if(VICINAE_GLYPH_WITH_ICU)
	target_link_libraries(${PROJECT_NAME} PUBLIC ICU::uc)
endif()

if(BUILD_TESTS AND VICINAE_GLYPH_WITH_ICU)
	set(TEST_TARGET ${PROJECT_NAME}-tests)
	find_package(Catch2 3 REQUIRED)
	add_executable(${TEST_TARGET} tests/main.cpp tests/tones.cpp tests/special-cases.cpp)
	target_link_libraries(${TEST_TARGET} PRIVATE Catch2::Catch2WithMain ${PROJECT_NAME})
	target_include_directories(${TEST_TARGET} PRIVATE .)
endif()