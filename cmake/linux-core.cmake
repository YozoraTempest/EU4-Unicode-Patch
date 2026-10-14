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
message(STATUS "Linux shared-core build: no EU4 game plugin is produced yet")
