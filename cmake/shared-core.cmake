# Shared source definitions for both native platform builds. The caller selects
# ICU and configures its compiler/runtime before including this file.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

set(EU4_UNICODE_CORE_TARGETS
    unicode_text unicode_services formatted_text unicode_editor unicode_search
    glyph_registry font_draw_batches font_assets)

add_library(unicode_text STATIC src/unicode_text.cpp src/native_text_event.cpp)
target_include_directories(unicode_text PUBLIC include PRIVATE vendor/utfcpp/source)
add_library(unicode_services STATIC src/unicode_services.cpp)
target_link_libraries(unicode_services PUBLIC unicode_text ${EU4_UNICODE_ICU_LIBRARIES})
add_library(formatted_text STATIC src/formatted_text.cpp src/formatted_text_cache.cpp)
target_link_libraries(formatted_text PUBLIC unicode_services)
add_library(unicode_editor STATIC src/unicode_editor.cpp src/editor_document.cpp)
target_link_libraries(unicode_editor PUBLIC unicode_services)

set(PINYIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/phrase-pinyin-data")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/pinyin_data.inc"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-pinyin-data.py"
        "${PINYIN_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}/generated/pinyin_data.inc"
    DEPENDS tools/generate-pinyin-data.py "${PINYIN_SOURCE}/pinyin.txt"
        "${PINYIN_SOURCE}/LICENSE" "${PINYIN_SOURCE}/source.json"
    VERBATIM)
add_library(unicode_search STATIC
    src/unicode_search.cpp src/unicode_search_fuzzy.cpp src/unicode_pinyin.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/pinyin_data.inc")
target_include_directories(unicode_search PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(unicode_search PUBLIC unicode_services)

add_library(glyph_registry STATIC src/glyph_registry.cpp)
target_include_directories(glyph_registry PUBLIC include)
target_link_libraries(glyph_registry PUBLIC Threads::Threads)
add_library(font_draw_batches STATIC src/font_draw_batches.cpp)
target_include_directories(font_draw_batches PUBLIC include)
add_library(font_assets STATIC src/font_assets.cpp)
target_include_directories(font_assets PUBLIC include)
