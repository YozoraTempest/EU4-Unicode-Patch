#pragma once
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <fontconfig/fcfreetype.h>

// Resolve the C rendering API from the runtime's dependency scope. Pango
// objects and their allocations stay with the matching GLib/Cairo stack.
#define EU4_UNICODE_RENDER_SYMBOLS(X) \
    X(FcConfigAppFontAddFile) \
    X(FcConfigDestroy) \
    X(FcFreeTypeQuery) \
    X(FcInitLoadConfigAndFonts) \
    X(FcPatternDestroy) \
    X(FcPatternGetString) \
    X(cairo_create) \
    X(cairo_destroy) \
    X(cairo_font_options_create) \
    X(cairo_font_options_destroy) \
    X(cairo_font_options_set_antialias) \
    X(cairo_font_options_set_hint_metrics) \
    X(cairo_font_options_set_hint_style) \
    X(cairo_image_surface_create) \
    X(cairo_image_surface_create_for_data) \
    X(cairo_image_surface_get_data) \
    X(cairo_image_surface_get_stride) \
    X(cairo_matrix_scale) \
    X(cairo_move_to) \
    X(cairo_recording_surface_create) \
    X(cairo_recording_surface_ink_extents) \
    X(cairo_scaled_font_create) \
    X(cairo_scaled_font_destroy) \
    X(cairo_scaled_font_get_ctm) \
    X(cairo_scaled_font_get_font_face) \
    X(cairo_scaled_font_get_font_matrix) \
    X(cairo_scaled_font_get_font_options) \
    X(cairo_scaled_font_status) \
    X(cairo_set_scaled_font) \
    X(cairo_set_source_rgba) \
    X(cairo_show_glyphs) \
    X(cairo_status) \
    X(cairo_status_to_string) \
    X(cairo_surface_destroy) \
    X(cairo_surface_flush) \
    X(cairo_surface_status) \
    X(cairo_surface_write_to_png) \
    X(cairo_translate) \
    X(g_free) \
    X(g_object_ref) \
    X(g_object_unref) \
    X(pango_attr_foreground_alpha_new) \
    X(pango_attr_foreground_new) \
    X(pango_attr_list_change) \
    X(pango_attr_list_new) \
    X(pango_attr_list_unref) \
    X(pango_attr_shape_new) \
    X(pango_cairo_context_set_font_options) \
    X(pango_cairo_context_set_resolution) \
    X(pango_cairo_font_get_scaled_font) \
    X(pango_cairo_font_map_new_for_font_type) \
    X(pango_cairo_show_layout) \
    X(pango_cairo_show_layout_line) \
    X(pango_context_set_base_dir) \
    X(pango_fc_font_map_set_config) \
    X(pango_find_base_dir) \
    X(pango_font_describe_with_absolute_size) \
    X(pango_font_description_free) \
    X(pango_font_description_get_family) \
    X(pango_font_description_get_size) \
    X(pango_font_description_new) \
    X(pango_font_description_set_absolute_size) \
    X(pango_font_description_set_family) \
    X(pango_font_map_create_context) \
    X(pango_layout_get_baseline) \
    X(pango_layout_get_extents) \
    X(pango_layout_get_iter) \
    X(pango_layout_get_line_count) \
    X(pango_layout_get_lines_readonly) \
    X(pango_layout_index_to_line_x) \
    X(pango_layout_index_to_pos) \
    X(pango_layout_iter_free) \
    X(pango_layout_iter_get_baseline) \
    X(pango_layout_iter_get_line_extents) \
    X(pango_layout_iter_get_line_readonly) \
    X(pango_layout_iter_get_line_yrange) \
    X(pango_layout_iter_get_run_extents) \
    X(pango_layout_iter_get_run_readonly) \
    X(pango_layout_iter_next_line) \
    X(pango_layout_iter_next_run) \
    X(pango_layout_line_get_x_ranges) \
    X(pango_layout_new) \
    X(pango_layout_set_alignment) \
    X(pango_layout_set_attributes) \
    X(pango_layout_set_auto_dir) \
    X(pango_layout_set_font_description) \
    X(pango_layout_set_text) \
    X(pango_layout_set_width) \
    X(pango_layout_set_wrap) \
    X(pango_layout_xy_to_index)

namespace eu4unicode::linux_native {
struct RenderApi {
#define EU4_RENDER_DECLARE(name) decltype(&::name) name;
    EU4_UNICODE_RENDER_SYMBOLS(EU4_RENDER_DECLARE)
#undef EU4_RENDER_DECLARE
    int (*pango_layout_iter_get_run_baseline)(PangoLayoutIter*);
    RenderApi();
};
RenderApi& render_api();
}

#ifndef EU4_UNICODE_RENDER_IMPLEMENTATION
#undef FcConfigAppFontAddFile
#define FcConfigAppFontAddFile (*::eu4unicode::linux_native::render_api().FcConfigAppFontAddFile)
#undef FcConfigDestroy
#define FcConfigDestroy (*::eu4unicode::linux_native::render_api().FcConfigDestroy)
#undef FcFreeTypeQuery
#define FcFreeTypeQuery (*::eu4unicode::linux_native::render_api().FcFreeTypeQuery)
#undef FcInitLoadConfigAndFonts
#define FcInitLoadConfigAndFonts (*::eu4unicode::linux_native::render_api().FcInitLoadConfigAndFonts)
#undef FcPatternDestroy
#define FcPatternDestroy (*::eu4unicode::linux_native::render_api().FcPatternDestroy)
#undef FcPatternGetString
#define FcPatternGetString (*::eu4unicode::linux_native::render_api().FcPatternGetString)
#undef cairo_create
#define cairo_create (*::eu4unicode::linux_native::render_api().cairo_create)
#undef cairo_destroy
#define cairo_destroy (*::eu4unicode::linux_native::render_api().cairo_destroy)
#undef cairo_font_options_create
#define cairo_font_options_create (*::eu4unicode::linux_native::render_api().cairo_font_options_create)
#undef cairo_font_options_destroy
#define cairo_font_options_destroy (*::eu4unicode::linux_native::render_api().cairo_font_options_destroy)
#undef cairo_font_options_set_antialias
#define cairo_font_options_set_antialias (*::eu4unicode::linux_native::render_api().cairo_font_options_set_antialias)
#undef cairo_font_options_set_hint_metrics
#define cairo_font_options_set_hint_metrics (*::eu4unicode::linux_native::render_api().cairo_font_options_set_hint_metrics)
#undef cairo_font_options_set_hint_style
#define cairo_font_options_set_hint_style (*::eu4unicode::linux_native::render_api().cairo_font_options_set_hint_style)
#undef cairo_image_surface_create
#define cairo_image_surface_create (*::eu4unicode::linux_native::render_api().cairo_image_surface_create)
#undef cairo_image_surface_create_for_data
#define cairo_image_surface_create_for_data (*::eu4unicode::linux_native::render_api().cairo_image_surface_create_for_data)
#undef cairo_image_surface_get_data
#define cairo_image_surface_get_data (*::eu4unicode::linux_native::render_api().cairo_image_surface_get_data)
#undef cairo_image_surface_get_stride
#define cairo_image_surface_get_stride (*::eu4unicode::linux_native::render_api().cairo_image_surface_get_stride)
#undef cairo_matrix_scale
#define cairo_matrix_scale (*::eu4unicode::linux_native::render_api().cairo_matrix_scale)
#undef cairo_move_to
#define cairo_move_to (*::eu4unicode::linux_native::render_api().cairo_move_to)
#undef cairo_recording_surface_create
#define cairo_recording_surface_create (*::eu4unicode::linux_native::render_api().cairo_recording_surface_create)
#undef cairo_recording_surface_ink_extents
#define cairo_recording_surface_ink_extents (*::eu4unicode::linux_native::render_api().cairo_recording_surface_ink_extents)
#undef cairo_scaled_font_create
#define cairo_scaled_font_create (*::eu4unicode::linux_native::render_api().cairo_scaled_font_create)
#undef cairo_scaled_font_destroy
#define cairo_scaled_font_destroy (*::eu4unicode::linux_native::render_api().cairo_scaled_font_destroy)
#undef cairo_scaled_font_get_ctm
#define cairo_scaled_font_get_ctm (*::eu4unicode::linux_native::render_api().cairo_scaled_font_get_ctm)
#undef cairo_scaled_font_get_font_face
#define cairo_scaled_font_get_font_face (*::eu4unicode::linux_native::render_api().cairo_scaled_font_get_font_face)
#undef cairo_scaled_font_get_font_matrix
#define cairo_scaled_font_get_font_matrix (*::eu4unicode::linux_native::render_api().cairo_scaled_font_get_font_matrix)
#undef cairo_scaled_font_get_font_options
#define cairo_scaled_font_get_font_options (*::eu4unicode::linux_native::render_api().cairo_scaled_font_get_font_options)
#undef cairo_scaled_font_status
#define cairo_scaled_font_status (*::eu4unicode::linux_native::render_api().cairo_scaled_font_status)
#undef cairo_set_scaled_font
#define cairo_set_scaled_font (*::eu4unicode::linux_native::render_api().cairo_set_scaled_font)
#undef cairo_set_source_rgba
#define cairo_set_source_rgba (*::eu4unicode::linux_native::render_api().cairo_set_source_rgba)
#undef cairo_show_glyphs
#define cairo_show_glyphs (*::eu4unicode::linux_native::render_api().cairo_show_glyphs)
#undef cairo_status
#define cairo_status (*::eu4unicode::linux_native::render_api().cairo_status)
#undef cairo_status_to_string
#define cairo_status_to_string (*::eu4unicode::linux_native::render_api().cairo_status_to_string)
#undef cairo_surface_destroy
#define cairo_surface_destroy (*::eu4unicode::linux_native::render_api().cairo_surface_destroy)
#undef cairo_surface_flush
#define cairo_surface_flush (*::eu4unicode::linux_native::render_api().cairo_surface_flush)
#undef cairo_surface_status
#define cairo_surface_status (*::eu4unicode::linux_native::render_api().cairo_surface_status)
#undef cairo_surface_write_to_png
#define cairo_surface_write_to_png (*::eu4unicode::linux_native::render_api().cairo_surface_write_to_png)
#undef cairo_translate
#define cairo_translate (*::eu4unicode::linux_native::render_api().cairo_translate)
#undef g_free
#define g_free (*::eu4unicode::linux_native::render_api().g_free)
#undef g_object_ref
#define g_object_ref (*::eu4unicode::linux_native::render_api().g_object_ref)
#undef g_object_unref
#define g_object_unref (*::eu4unicode::linux_native::render_api().g_object_unref)
#undef pango_attr_foreground_alpha_new
#define pango_attr_foreground_alpha_new (*::eu4unicode::linux_native::render_api().pango_attr_foreground_alpha_new)
#undef pango_attr_foreground_new
#define pango_attr_foreground_new (*::eu4unicode::linux_native::render_api().pango_attr_foreground_new)
#undef pango_attr_list_change
#define pango_attr_list_change (*::eu4unicode::linux_native::render_api().pango_attr_list_change)
#undef pango_attr_list_new
#define pango_attr_list_new (*::eu4unicode::linux_native::render_api().pango_attr_list_new)
#undef pango_attr_list_unref
#define pango_attr_list_unref (*::eu4unicode::linux_native::render_api().pango_attr_list_unref)
#undef pango_attr_shape_new
#define pango_attr_shape_new (*::eu4unicode::linux_native::render_api().pango_attr_shape_new)
#undef pango_cairo_context_set_font_options
#define pango_cairo_context_set_font_options (*::eu4unicode::linux_native::render_api().pango_cairo_context_set_font_options)
#undef pango_cairo_context_set_resolution
#define pango_cairo_context_set_resolution (*::eu4unicode::linux_native::render_api().pango_cairo_context_set_resolution)
#undef pango_cairo_font_get_scaled_font
#define pango_cairo_font_get_scaled_font (*::eu4unicode::linux_native::render_api().pango_cairo_font_get_scaled_font)
#undef pango_cairo_font_map_new_for_font_type
#define pango_cairo_font_map_new_for_font_type (*::eu4unicode::linux_native::render_api().pango_cairo_font_map_new_for_font_type)
#undef pango_cairo_show_layout
#define pango_cairo_show_layout (*::eu4unicode::linux_native::render_api().pango_cairo_show_layout)
#undef pango_cairo_show_layout_line
#define pango_cairo_show_layout_line (*::eu4unicode::linux_native::render_api().pango_cairo_show_layout_line)
#undef pango_context_set_base_dir
#define pango_context_set_base_dir (*::eu4unicode::linux_native::render_api().pango_context_set_base_dir)
#undef pango_fc_font_map_set_config
#define pango_fc_font_map_set_config (*::eu4unicode::linux_native::render_api().pango_fc_font_map_set_config)
#undef pango_find_base_dir
#define pango_find_base_dir (*::eu4unicode::linux_native::render_api().pango_find_base_dir)
#undef pango_font_describe_with_absolute_size
#define pango_font_describe_with_absolute_size (*::eu4unicode::linux_native::render_api().pango_font_describe_with_absolute_size)
#undef pango_font_description_free
#define pango_font_description_free (*::eu4unicode::linux_native::render_api().pango_font_description_free)
#undef pango_font_description_get_family
#define pango_font_description_get_family (*::eu4unicode::linux_native::render_api().pango_font_description_get_family)
#undef pango_font_description_get_size
#define pango_font_description_get_size (*::eu4unicode::linux_native::render_api().pango_font_description_get_size)
#undef pango_font_description_new
#define pango_font_description_new (*::eu4unicode::linux_native::render_api().pango_font_description_new)
#undef pango_font_description_set_absolute_size
#define pango_font_description_set_absolute_size (*::eu4unicode::linux_native::render_api().pango_font_description_set_absolute_size)
#undef pango_font_description_set_family
#define pango_font_description_set_family (*::eu4unicode::linux_native::render_api().pango_font_description_set_family)
#undef pango_font_map_create_context
#define pango_font_map_create_context (*::eu4unicode::linux_native::render_api().pango_font_map_create_context)
#undef pango_layout_get_baseline
#define pango_layout_get_baseline (*::eu4unicode::linux_native::render_api().pango_layout_get_baseline)
#undef pango_layout_get_extents
#define pango_layout_get_extents (*::eu4unicode::linux_native::render_api().pango_layout_get_extents)
#undef pango_layout_get_iter
#define pango_layout_get_iter (*::eu4unicode::linux_native::render_api().pango_layout_get_iter)
#undef pango_layout_get_line_count
#define pango_layout_get_line_count (*::eu4unicode::linux_native::render_api().pango_layout_get_line_count)
#undef pango_layout_get_lines_readonly
#define pango_layout_get_lines_readonly (*::eu4unicode::linux_native::render_api().pango_layout_get_lines_readonly)
#undef pango_layout_index_to_line_x
#define pango_layout_index_to_line_x (*::eu4unicode::linux_native::render_api().pango_layout_index_to_line_x)
#undef pango_layout_index_to_pos
#define pango_layout_index_to_pos (*::eu4unicode::linux_native::render_api().pango_layout_index_to_pos)
#undef pango_layout_iter_free
#define pango_layout_iter_free (*::eu4unicode::linux_native::render_api().pango_layout_iter_free)
#undef pango_layout_iter_get_baseline
#define pango_layout_iter_get_baseline (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_baseline)
#undef pango_layout_iter_get_line_extents
#define pango_layout_iter_get_line_extents (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_line_extents)
#undef pango_layout_iter_get_line_readonly
#define pango_layout_iter_get_line_readonly (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_line_readonly)
#undef pango_layout_iter_get_line_yrange
#define pango_layout_iter_get_line_yrange (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_line_yrange)
#undef pango_layout_iter_get_run_baseline
#define pango_layout_iter_get_run_baseline (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_run_baseline)
#undef pango_layout_iter_get_run_extents
#define pango_layout_iter_get_run_extents (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_run_extents)
#undef pango_layout_iter_get_run_readonly
#define pango_layout_iter_get_run_readonly (*::eu4unicode::linux_native::render_api().pango_layout_iter_get_run_readonly)
#undef pango_layout_iter_next_line
#define pango_layout_iter_next_line (*::eu4unicode::linux_native::render_api().pango_layout_iter_next_line)
#undef pango_layout_iter_next_run
#define pango_layout_iter_next_run (*::eu4unicode::linux_native::render_api().pango_layout_iter_next_run)
#undef pango_layout_line_get_x_ranges
#define pango_layout_line_get_x_ranges (*::eu4unicode::linux_native::render_api().pango_layout_line_get_x_ranges)
#undef pango_layout_new
#define pango_layout_new (*::eu4unicode::linux_native::render_api().pango_layout_new)
#undef pango_layout_set_alignment
#define pango_layout_set_alignment (*::eu4unicode::linux_native::render_api().pango_layout_set_alignment)
#undef pango_layout_set_attributes
#define pango_layout_set_attributes (*::eu4unicode::linux_native::render_api().pango_layout_set_attributes)
#undef pango_layout_set_auto_dir
#define pango_layout_set_auto_dir (*::eu4unicode::linux_native::render_api().pango_layout_set_auto_dir)
#undef pango_layout_set_font_description
#define pango_layout_set_font_description (*::eu4unicode::linux_native::render_api().pango_layout_set_font_description)
#undef pango_layout_set_text
#define pango_layout_set_text (*::eu4unicode::linux_native::render_api().pango_layout_set_text)
#undef pango_layout_set_width
#define pango_layout_set_width (*::eu4unicode::linux_native::render_api().pango_layout_set_width)
#undef pango_layout_set_wrap
#define pango_layout_set_wrap (*::eu4unicode::linux_native::render_api().pango_layout_set_wrap)
#undef pango_layout_xy_to_index
#define pango_layout_xy_to_index (*::eu4unicode::linux_native::render_api().pango_layout_xy_to_index)
#endif

