#pragma once
// A colourwork chart: one pixel = one colour cell, like a paper knitting
// chart. Each cell renders as 2x2 fine stitches, or one defined stitch.
//
//   fully transparent pixel -> the window's own yarn colour
//   opaque pixel            -> that literal colour, knitted as a contrast yarn
//
// The image's width and height are the pattern repeat.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KNIT_CHART_MAX 128

struct knit_chart {
  char name[64];
  int w, h;
  uint32_t* px;           // ARGB, row 0 = top of the chart as authored
  bool solid_corners;     // use a solid yarn patch on the rounded arcs
  uint32_t corner_color;  // zero follows the base yarn, otherwise opaque ARGB
  uint32_t cuff_color;    // optional curved outer cuff, one third of the band
  bool round_dots;        // soften the 2x2 dot blocks into filled circles
  bool fitted_repeat;     // symmetric charts fit complete repeats for matching mitres
  bool sculpted_yarn;     // raised, path-rendered stitches, cached per tile
  bool defined_yarn;      // larger, clearer stitches for otherwise flat colour blocks
  bool generated;         // built at runtime from an app icon, never offered by name
  bool custom;            // drawn by the user (a PNG in the charts folder), shown as drawn
};

extern struct knit_chart g_charts[KNIT_CHART_MAX];
extern int g_chart_count;
// Bumped by every knit_charts_load and whenever a chart is replaced in place.
// Anything caching a chart index or a tile must re-check this.
extern unsigned g_charts_generation;
extern int g_chart_active;   // -1 = plain, no colourwork

/// The folder for user-authored PNG charts:
/// $XDG_CONFIG_HOME/window-sweaters/charts (created on first use).
const char* knit_charts_dir(void);

/// Reload the built-in collection, then sorted .png files in `dir` (NULL for
/// built-ins only). A user PNG with the same name replaces its built-in chart.
/// Preserves the active chart by name and returns the total.
int knit_charts_load(const char* dir);

/// Look up a chart by name (without .png). Returns index or -1.
int knit_chart_index(const char* name);

#ifdef __cplusplus
}
#endif
