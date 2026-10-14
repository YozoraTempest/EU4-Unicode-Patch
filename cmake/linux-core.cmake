# Native Linux adaptation starts with the shared Unicode algorithms. Game
# loading, ABI hooks, rendering and IME require the Linux executable profile.
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
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/safetyhook EXCLUDE_FROM_ALL)
add_library(EU4UnicodePatchLinux SHARED
    src/linux/bootstrap.cpp src/linux/profile.cpp
    src/linux/localization.cpp src/linux/bitmap_fonts.cpp
    src/linux/mod_registry.cpp src/legacy_text.cpp)
target_include_directories(EU4UnicodePatchLinux PRIVATE include)
target_compile_features(EU4UnicodePatchLinux PRIVATE cxx_std_23)
target_compile_options(EU4UnicodePatchLinux PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(EU4UnicodePatchLinux PRIVATE unicode_text glyph_registry
    safetyhook::safetyhook OpenSSL::Crypto ${CMAKE_DL_LIBS})
set_target_properties(EU4UnicodePatchLinux PROPERTIES OUTPUT_NAME eu4_unicode)
message(STATUS "Building the native EU4 1.37.5 Linux x86_64 plugin.")
