// Renders the sweater collection plates with the Cairo port, laid out exactly
// like tests/render.c --individual in the macOS project, so the two renderers
// can be compared pixel by pixel (see tests/compare.c).
//
//   render-test --individual DIR   app-01.png ... app-37.png
//   render-test --plain PATH       stockinette, rib and garter samples
#include "../core/knit.h"
#include "../core/chart.h"
#include "../core/apps.h"
#include "catalogue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static cairo_t* canvas(int w, int h, int scale, cairo_surface_t** out) {
  *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * scale, h * scale);
  cairo_t* c = cairo_create(*out);
  cairo_scale(c, scale, scale);
  return c;
}

static void rounded(cairo_t* c, double x, double y, double w, double h, double r) {
  cairo_new_sub_path(c);
  cairo_arc(c, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_arc(c, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_arc(c, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_arc(c, x + r, y + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(c);
}

static void text_at(cairo_t* c, double x, double y, const char* text, double size) {
  cairo_save(c);
  cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(c, size);
  cairo_set_source_rgb(c, .19, .20, .18);
  cairo_move_to(c, x, y);
  cairo_show_text(c, text);
  cairo_restore(c);
}

static void window_fill(cairo_t* c, double x, double y, double w, double h) {
  cairo_set_source_rgb(c, 1, 1, 1);
  rounded(c, x, y, w, h, 10);
  cairo_fill(c);
}

static void individual(const char* directory) {
  knit_charts_load(NULL);
  for (int i = 0; i < CATALOGUE_COUNT; i++) {
    const struct catalogue_entry* entry = &catalogue[i];
    const struct app_rule* rule = knit_app_rule(entry->match); assert(rule);
    int chart = knit_chart_index(rule->chart); assert(chart >= 0);
    cairo_surface_t* s;
    cairo_t* c = canvas(720, 530, 2, &s);
    cairo_set_source_rgb(c, .977, .969, .953);
    cairo_paint(c);
    text_at(c, 38, 47, entry->name, 29);
    text_at(c, 38, 74, entry->pattern, 14);
    double wx = 48, wy = 112, ww = 620, wh = 140;
    knit_draw(c, wx, wy, ww, wh, 10, 10, rule->color, chart, 0, 1);
    window_fill(c, wx, wy, ww, wh);
    text_at(c, 68, 154, "10-point window border", 13);
    text_at(c, 38, 286, "10-point stitch geometry / enlarged 4x", 12);
    cairo_save(c);
    cairo_rectangle(c, 38, 301, 640, 40); cairo_clip(c);
    cairo_translate(c, 38 - 4 * 100, 301 - 4 * 102);
    cairo_scale(c, 4, 4);
    knit_draw(c, wx, wy, ww, wh, 10, 10, rule->color, chart, 0, 1);
    cairo_restore(c);
    text_at(c, 38, 375, "All four corners / enlarged from the same window", 12);
    const double sx[] = {38, 638, 638, 38}, sy[] = {102, 102, 222, 222};
    const char* labels[] = {"Top left", "Top right", "Bottom right", "Bottom left"};
    for (int corner = 0; corner < 4; corner++) {
      double x = 58 + corner * 166, y = 390;
      cairo_save(c);
      cairo_rectangle(c, x, y, 80, 80); cairo_clip(c);
      cairo_translate(c, x - 2 * sx[corner], y - 2 * sy[corner]);
      cairo_scale(c, 2, 2);
      knit_draw(c, wx, wy, ww, wh, 10, 10, rule->color, chart, 0, 1);
      window_fill(c, wx, wy, ww, wh);
      cairo_restore(c);
      text_at(c, x, 496, labels[corner], 12);
    }
    char output[1024];
    snprintf(output, sizeof output, "%s/app-%02d.png", directory, i + 1);
    assert(cairo_surface_write_to_png(s, output) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(c); cairo_surface_destroy(s);
  }
}

static void plain(const char* path) {
  knit_charts_load(NULL);
  cairo_surface_t* s;
  cairo_t* c = canvas(720, 3 * 170 + 40, 2, &s);
  cairo_set_source_rgb(c, .977, .969, .953);
  cairo_paint(c);
  for (int stitch = 0; stitch < KNIT_STITCH_COUNT; stitch++) {
    g_knit_stitch = stitch;
    double y = 40 + stitch * 170;
    text_at(c, 38, y, g_knit_stitch_names[stitch], 18);
    knit_draw(c, 48, y + 30, 620, 110, 10, 12, 0xff4e8098u, -1, 0, 1);
    window_fill(c, 48, y + 30, 620, 110);
  }
  g_knit_stitch = KNIT_STOCKINETTE;
  assert(cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS);
  cairo_destroy(c); cairo_surface_destroy(s);
}

// A tiled desktop as Hyprland would lay it out: four windows, 12 pt sweaters
// in By App mode, matched by their Linux app ids. Rendered with the renderer
// only; this is not a screenshot of a running compositor.
static void desktop(const char* path) {
  knit_charts_load(NULL);
  knit_pattern_select("by-app");
  const struct { const char* app; double x, y, w, h; } windows[] = {
    { "firefox",               24,  24, 760, 672 },
    { "code",                 816,  24, 760, 320 },
    { "com.mitchellh.ghostty", 816, 376, 368, 320 },
    { "org.gnome.Nautilus",   1208, 376, 368, 320 },
  };
  cairo_surface_t* s;
  cairo_t* c = canvas(1600, 720, 1, &s);
  cairo_pattern_t* sky = cairo_pattern_create_linear(0, 0, 1600, 720);
  cairo_pattern_add_color_stop_rgb(sky, 0, .23, .25, .33);
  cairo_pattern_add_color_stop_rgb(sky, 1, .40, .33, .38);
  cairo_set_source(c, sky); cairo_paint(c); cairo_pattern_destroy(sky);
  for (size_t i = 0; i < sizeof windows / sizeof windows[0]; i++) {
    // Hyprland reserves the band inside the tile, so the window shrinks by it.
    double band = 12, x = windows[i].x + band, y = windows[i].y + band;
    double w = windows[i].w - 2 * band, h = windows[i].h - 2 * band;
    const struct app_rule* rule = knit_app_rule(windows[i].app);
    uint32_t yarn = rule ? rule->color : knit_color_for_app(windows[i].app);
    knit_draw(c, x, y, w, h, 10, (float)band, yarn, knit_pattern_for_app(windows[i].app), 0, 1);
    cairo_set_source_rgb(c, .98, .98, .97);
    rounded(c, x, y, w, h, 10); cairo_fill(c);
    text_at(c, x + 20, y + 34, windows[i].app, 16);
  }
  assert(cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS);
  cairo_destroy(c); cairo_surface_destroy(s);
}

int main(int argc, char** argv) {
  if (argc == 3 && strcmp(argv[1], "--desktop") == 0) { desktop(argv[2]); return 0; }
  if (argc == 3 && strcmp(argv[1], "--individual") == 0) { individual(argv[2]); return 0; }
  if (argc == 3 && strcmp(argv[1], "--plain") == 0) { plain(argv[2]); return 0; }
  fprintf(stderr, "usage: %s --individual DIR | --plain PATH | --desktop PATH\n", argv[0]);
  return 2;
}
