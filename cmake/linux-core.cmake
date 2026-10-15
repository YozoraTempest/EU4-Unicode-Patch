# Shared algorithms and the separately verified native Linux ELF adapter.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(ICU REQUIRED COMPONENTS uc i18n)
set(EU4_UNICODE_ICU_LIBRARIES ICU::uc ICU::i18n)
include(cmake/shared-core.cmake)
enable_testing()

foreach(module IN LISTS EU4_UNICODE_CORE_TARGETS)
    target_compile_options(${module} PRIVATE -Wall -Wextra -Wpedantic)
    add_executable(${module}_tests tests/${module}_tests.cpp)
    target_link_libraries(${module}_tests PRIVATE ${module})
    add_test(NAME ${module} COMMAND ${module}_tests)
endforeach()
add_executable(pinyin_fuzzy_tests tests/pinyin_fuzzy_tests.cpp)
target_link_libraries(pinyin_fuzzy_tests PRIVATE unicode_search)
add_test(NAME pinyin_fuzzy COMMAND pinyin_fuzzy_tests)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pangocairo pangofc fontconfig)
add_library(unicode_layout STATIC src/linux/unicode_layout.cpp)
target_link_libraries(unicode_layout PUBLIC unicode_services PkgConfig::PANGO)
target_compile_options(unicode_layout PRIVATE -Wall -Wextra -Wpedantic)
add_library(shaped_paragraph STATIC src/shaped_paragraph.cpp src/formatted_paragraph.cpp)
target_link_libraries(shaped_paragraph PUBLIC unicode_layout formatted_text)
add_library(scalar_glyph STATIC src/scalar_glyph.cpp)
target_link_libraries(scalar_glyph PUBLIC unicode_layout glyph_registry)
foreach(module unicode_layout shaped_paragraph formatted_paragraph)
    add_executable(${module}_tests tests/${module}_tests.cpp)
    if(module STREQUAL "formatted_paragraph")
        target_link_libraries(${module}_tests PRIVATE shaped_paragraph)
    else()
        target_link_libraries(${module}_tests PRIVATE ${module})
    endif()
    add_test(NAME ${module} COMMAND ${module}_tests)
endforeach()
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/safetyhook EXCLUDE_FROM_ALL)
add_library(EU4UnicodePatchLinux SHARED
    src/linux/bootstrap.cpp src/linux/profile.cpp
    src/linux/localization.cpp src/linux/script_bom.cpp src/linux/bitmap_fonts.cpp src/linux/map_names.cpp
    src/linux/mod_registry.cpp src/linux/editor_input.cpp
    src/linux/multiplayer_name.cpp src/linux/search.cpp src/linux/ime.cpp src/linux/name_presence.cpp src/legacy_text.cpp)
target_include_directories(EU4UnicodePatchLinux PRIVATE include vendor/picosha2)
target_compile_features(EU4UnicodePatchLinux PRIVATE cxx_std_23)
target_compile_options(EU4UnicodePatchLinux PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(EU4UnicodePatchLinux PRIVATE unicode_text unicode_services unicode_search name_policy glyph_registry
    safetyhook::safetyhook ${CMAKE_DL_LIBS})
set_target_properties(EU4UnicodePatchLinux PROPERTIES OUTPUT_NAME eu4_unicode)
target_link_options(EU4UnicodePatchLinux PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/linux/exports.map")
set_property(TARGET EU4UnicodePatchLinux APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/exports.map")
add_executable(linux_loader_fixture tests/linux_loader_fixture.cpp)
target_compile_options(linux_loader_fixture PRIVATE -fno-pie)
target_link_options(linux_loader_fixture PRIVATE -no-pie)
set_target_properties(linux_loader_fixture PROPERTIES OUTPUT_NAME eu4
    ENABLE_EXPORTS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/loader-fixture")
add_test(NAME linux_loader COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/linux_loader_test.py"
    "$<TARGET_FILE:EU4UnicodePatchLinux>" "$<TARGET_FILE:linux_loader_fixture>"
    "${CMAKE_SOURCE_DIR}/tools/launch-linux.sh")
message(STATUS "Building the native EU4 1.37.5 Linux x86_64 plugin.")
