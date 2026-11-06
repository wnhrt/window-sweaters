// The built-in sweater collection and user PNG charts (port of src/chart.c).
#include "chart.h"
#include "paths.h"
#include <cairo.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

const char* knit_charts_dir(void) {
  static char dir[1100];
  if (dir[0]) return dir;
  const char* base = knit_config_dir();
  if (!base[0]) return "";
  snprintf(dir, sizeof dir, "%s/charts", base);
  mkdir(dir, 0755);
  return dir;
}

struct knit_chart g_charts[KNIT_CHART_MAX];
int g_chart_count = 0;
int g_chart_active = -1;
unsigned g_charts_generation;

// The collection's repeats, as data rather than bundled PNGs, so it works on
// first launch and when the user chart folder is unavailable. A dot keeps the
// app's main yarn; a..f are the contrast yarns listed after the rows.
struct collection_chart {
  const char* name;
  const char* rows[12];
  uint32_t yarn[6]; // a..f
};

static const struct collection_chart k_collection[] = {
  { "atelier-finder", {
      "..aaaa....aaaa..", "..aaaa....aaaa..", "..aaaa....aaaa..",
      "aa....aaaa....aa", "aa....aaaa....aa", "b...b......b...b" },
    { 0xffb7e4f7u, 0xff24558bu } },
  { "atelier-terminal", {
      "aa....aa", "aa....aa", "aa....aa",
      "aa....aa", "aa....aa", "aa....aa" },
    { 0xffa5bda0u } },
  { "atelier-grok", {
      "aaa.b..b.aaa", "aaa.b..b.aaa", "aaa.b..b.aaa",
      "aaa.b..b.aaa", "aaa.b..b.aaa", "aaa.b..b.aaa" },
    { 0xffeeece4u, 0xff868a88u } },
  { "atelier-teams", {
      "a....aa....a", "aa........aa", ".aa......aa.",
      "..aa....aa..", "...aa..aa...", "....aaaa...." },
    { 0xfff4efeeu } },
  { "atelier-claude", {
      "............", "..a.....a...", ".aaa...aaa..",
      "..a.....a...", "............", "............" },
    { 0xfff7e8c5u } },
  { "atelier-codex", {
      "aa.....b....", "aa.....b....", "aa.....b....",
      "aa.....b....", "aa.....b....", "aa.....b...." },
    { 0xffe6e6ceu, 0xffe9bfcbu } },
  { "atelier-spotify", {
      "...aaa...aaa", "...aaa...aaa", "...aaa...aaa",
      "aaa...aaa...", "aaa...aaa...", "aaa...aaa..." },
    { 0xffe5d586u } },
  { "atelier-notion", {
      "...aaa...aaa", "...aaa...aaa", "...aaa...aaa",
      "aaa...aaa...", "aaa...aaa...", "aaa...aaa..." },
    { 0xff494947u } },
  { "atelier-whatsapp", {
      "aaaa....b...", "aaaa....b...", "aaaa....b...",
      "aaaa....b...", "aaaa....b...", "aaaa....b..." },
    { 0xffdde9bcu, 0xffe3a2b8u } },
  { "atelier-figma", {
      "aabb....cc..", "aabb....cc..", "aabb....cc..",
      "....ddee....", "....ddee....", "....ddee...." },
    { 0xffeaaf96u, 0xffdcd092u, 0xff8dbccfu, 0xffafc5a1u, 0xffded3e9u } },
  { "atelier-chrome", {
      "....aaaaaaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb........dddddddd........cccccccc....", "....aaaaaaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb........dddddddd........cccccccc....", "....aaaaaaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb........dddddddd........cccccccc....",
      "aaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb........dddddddd........cccccccc........aaaa", "aaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb........dddddddd........cccccccc........aaaa", "aaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb........dddddddd........cccccccc........aaaa" },
    { 0xffd8675bu, 0xff6ba776u, 0xff4285f4u, 0xffedcc70u } },
  { "atelier-paper", {
      "..aaaa..", ".a...a..", ".a.b.a..",
      ".aaaaa..", ".aaa....", "........" },
    { 0xfff5f5f2u, 0xff5f8bceu } },
  { "atelier-safari", {
      "..a.....a...", ".aaa...aaa..", "aabaa.aabaa.",
      ".aaa...aaa..", "..a.....a...", "............" },
    { 0xfff6f2e8u, 0xffed7066u } },
  { "atelier-firefox", {
      "a.....a.....", "aa....aa....", ".aa....aa...",
      "..bb....bb..", "...bb....bb.", "....b.....b." },
    { 0xffff873eu, 0xffffc167u } },
  { "atelier-cursor", {
      "a....aa....a", "aa........aa", ".aa......aa.",
      "..aa....aa..", "...aa..aa...", "....aaaa...." },
    { 0xffc4c2b9u } },
  { "atelier-slack", {
      ".a......b...", "aaa....bbb..", ".a......b...",
      "....c......d", "...ccc....dd", "....c......d" },
    { 0xff63c5dfu, 0xff82c5a6u, 0xffedc45eu, 0xffe986a3u } },
  { "atelier-zoom", {
      ".aaaa...aaaa", ".abba...abba", ".abba...abba",
      ".aaaa...aaaa", "............", "bbbbbbbbbbbb" },
    { 0xfff6f8f4u, 0xff9ec7f5u } },
  { "atelier-telegram", {
      "a.....a.....", "aa....aa....", "aba...aba...",
      ".aba...aba..", "..aa....aa..", "...a.....a.." },
    { 0xfff5f5ecu, 0xffaddde8u } },
  { "atelier-messages", {
      "................", "..aa......aa....", "..aa......aa....",
      "................", "......bb......bb", "......bb......bb" },
    { 0xfff8f7e8u, 0xffb7db8du } },
  { "atelier-mail", {
      "a.....a.....", ".a...a.a...a", "..a.a...a.a.",
      "...a.....a..", "............", "bbbbbbbbbbbb" },
    { 0xfff5f5f0u, 0xffb3d8eeu } },
  { "atelier-notes", {
      "aaaaaaaaaaaa", "aaaaaaaaaaaa", "............",
      "............", "............", "bbbbbbbbbbbb" },
    { 0xffefc852u, 0xfffffcf3u } },
  { "atelier-calendar", {
      "aaaaaaaaaaaa", "aaaaaaaaaaaa", "............",
      "............", "............", "............" },
    { 0xffe55c52u } },
  { "atelier-reminders", {
      "a.dd..b.dd..", "............", "c.dd..a.dd..",
      "............", "b.dd..c.dd..", "............" },
    { 0xff579fdeu, 0xffe5787au, 0xffeba451u, 0xffccc6bau } },
  { "atelier-music", {
      "a.....a.....", "aa....aa....", ".aa....aa...",
      "..bb....bb..", "...bb....bb.", "....b.....b." },
    { 0xfff7acc0u, 0xfffff0dbu } },
  { "atelier-photos", {
      ".a...c...e..", "aaa.ccc.eee.", ".a...c...e..",
      "...b...d...f", "..bbb.ddd.ff", "...b...d...f" },
    { 0xffefa470u, 0xffeccb67u, 0xff95ba78u, 0xff7bbbc9u, 0xff9991c1u, 0xffd98bacu } },
  { "atelier-preview", {
      "bbbbbbbbbbbb", "b.....b.....", "b..a..b..a..",
      "b.aaa.b.aaa.", "baaaaabaaaaa", "bbbbbbbbbbbb" },
    { 0xfff2f4efu, 0xffaacfddu } },
  { "atelier-word", {
      "a....aa....a", "aa........aa", ".aa......aa.",
      "..bb....bb..", "...bb..bb...", "....bbbb...." },
    { 0xfff3f1e6u, 0xff82a8deu } },
  { "atelier-excel", {
      "aa...baa...b", "aa...baa...b", "bbbbbbbbbbbb",
      "..aa.b..aa.b", "..aa.b..aa.b", "bbbbbbbbbbbb" },
    { 0xff82b99au, 0xffdbead0u } },
  { "atelier-powerpoint", {
      ".aaaa...aaaa", ".abba...abba", ".abba...abba",
      ".aaaa...aaaa", "............", "..b.....b..." },
    { 0xffeea58au, 0xfff5d9b8u } },
  { "atelier-outlook", {
      "aaa...aaa...", "aba...aba...", "aab...aab...",
      "...aaa...aaa", "...aba...aba", "...aab...aab" },
    { 0xff8ac6ecu, 0xfff5f5eau } },
  { "atelier-vscode", {
      "a....ba....b", ".a..ab.a..ab", "..aa.b..aa.b",
      "..aa.b..aa.b", ".a..ab.a..ab", "a....ba....b" },
    { 0xffb6e1edu, 0xff17374fu } },
  { "atelier-photoshop", {
      "aaaa..aaaa..", "a..a..a..a..", "aaaa..aaaa..",
      "...bbb...bbb", "...b.b...b.b", "...bbb...bbb" },
    { 0xff58b7e9u, 0xff8cbad0u } },
  { "atelier-illustrator", {
      "...baaaab...", "...baaaab...", "...baaaab...",
      "...baaaab...", "...baaaab...", "...baaaab..." },
    { 0xfff5a13du, 0xfff8d2a0u } },
  { "atelier-granola", {
      "...aaaaaa...", "...aaaaaa...", "...aaaaaa...",
      "...aaaaaa...", "...aaaaaa...", "...aaaaaa..." },
    { 0xff8ba66au } },
  { "atelier-chatgpt", {
      "a....aa....a", "aa........aa", ".aa......aa.",
      "..aa....aa..", "...aa..aa...", "....aaaa...." },
    { 0xfff6f0deu } },
  { "atelier-ghostty", {
      "aa..........", "aa..........", "aa..........",
      "aa..........", "aa..........", "aa.........." },
    { 0xffecebe4u } },
  { "braid", {
      "abb........a", "a.bb......aa", "...bb....aaa",
      "....bb..aaa.", ".....bbaaa..", "......bba...",
      ".....aabb...", "....aaa.bb..", "...aaa...bb.",
      "..aaa.....bb", "baaa.......b", "bba........." },
    { 0xfff6f0deu, 0xfff078aau } },
  { "blockstripe", {
      "...aaa" },
    { 0xffffffffu } },
  { "checker", {
      "..aa", "..aa", "aa..",
      "aa.." },
    { 0xfff2eee4u } },
  { "seedling", {
      "....a.", "....a.", "...a.a",
      ".a....", ".a....", "a.a..." },
    { 0xff96d6a0u } },
  { "trim", {
      "...a....a.", "..........", "bb...bb...",
      "..........", "c...cc...c", "cc...cc..." },
    { 0xffffffffu, 0xfff58220u, 0xffd61e6eu } },
  { "picnic", {
      "...aaa...aaa", "...aaa...aaa", "...aaa...aaa",
      "aaa...aaa...", "aaa...aaa...", "aaa...aaa..." },
    { 0xfff4e6bfu } },
  { "ribbon", {
      "aa.....b....", "aa.....b....", "aa.....b....",
      "aa.....b....", "aa.....b....", "aa.....b...." },
    { 0xfff5e8cfu, 0xffe3a1b3u } },
  { "posy", {
      "............", ".aa.aa......", ".aaaaa......",
      "..aba.......", ".a...a......", "............" },
    { 0xfff0d1dbu, 0xfff2dd9du } },
  { "twinkle", {
      "............", "..a.....a...", ".aaa...aaa..",
      "..a.....a...", "............", "............" },
    { 0xfff5e6bfu } },
  { "candy-stripe", {
      "aaa...bbb...", "aaa...bbb...", "aaa...bbb...",
      "aaa...bbb...", "aaa...bbb...", "aaa...bbb..." },
    { 0xfff5e8cfu, 0xffe3a1b3u } },
  { "zigzag", {
      "a....aa....a", "aa........aa", ".aa......aa.",
      "..aa....aa..", "...aa..aa...", "....aaaa...." },
    { 0xfff6f0deu } },   // cream; a pale app gets a deeper shade (autoyarn.m)
};

// Solid patches finish patterns that otherwise collide at the mitre.
// A zero colour follows the app's own base yarn, including personal palettes.
static const struct { const char* chart; uint32_t color; } k_corner_styles[] = {
  { "atelier-whatsapp", 0 },       // keep its stripe joins quiet
};

_Static_assert(sizeof k_collection / sizeof k_collection[0] <= KNIT_CHART_MAX,
               "Built-in collection exceeds chart capacity");

static void load_collection(void) {
  for (size_t i = 0; i < sizeof k_collection / sizeof k_collection[0]; i++) {
    const struct collection_chart* spec = &k_collection[i];
    int w = (int)strlen(spec->rows[0]);
    int h = 0;
    while (h < 12 && spec->rows[h]) h++;
    uint32_t* pixels = calloc((size_t)w * h, sizeof *pixels);
    if (!pixels) continue;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        char yarn = spec->rows[y][x];
        if (yarn >= 'a' && yarn <= 'f')
          pixels[y * w + x] = spec->yarn[yarn - 'a'];
      }
    struct knit_chart* chart = &g_charts[g_chart_count++];
    snprintf(chart->name, sizeof chart->name, "%s", spec->name);
    chart->w = w;
    chart->h = h;
    chart->px = pixels;
    for (size_t corner = 0; corner < sizeof k_corner_styles / sizeof k_corner_styles[0]; corner++)
      if (strcmp(spec->name, k_corner_styles[corner].chart) == 0) {
        chart->solid_corners = true;
        chart->corner_color = k_corner_styles[corner].color;
        break;
      }
    if (strcmp(spec->name, "atelier-notes") == 0
        || strcmp(spec->name, "atelier-calendar") == 0) {
      // Move the outer cuff out of the rectangular chart: its coloured rows
      // otherwise repeat into the inner corner and make angular fragments.
      chart->cuff_color = spec->yarn[0];
      for (int cell = 0; cell < w * h; cell++)
        if (pixels[cell] == chart->cuff_color) pixels[cell] = 0;
    }
    chart->round_dots = strcmp(spec->name, "atelier-messages") == 0;
    chart->fitted_repeat = strcmp(spec->name, "atelier-finder") == 0
                         || strcmp(spec->name, "atelier-terminal") == 0
                         || strcmp(spec->name, "atelier-grok") == 0
                         || strcmp(spec->name, "atelier-granola") == 0
                         || strcmp(spec->name, "atelier-illustrator") == 0
                         || strcmp(spec->name, "atelier-chrome") == 0;
    chart->sculpted_yarn = !chart->round_dots;
    chart->defined_yarn = true;
  }
}

static int load_one(const char* path, const char* name, struct knit_chart* out) {
  cairo_surface_t* img = cairo_image_surface_create_from_png(path);
  if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(img); return 0; }
  int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img);
  if (w < 1 || h < 1 || w > 256 || h > 256) { cairo_surface_destroy(img); return 0; }

  // Normalise any PNG format to premultiplied ARGB, as CoreGraphics did.
  cairo_surface_t* argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t* cr = cairo_create(argb);
  cairo_set_source_surface(cr, img, 0, 0);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(img);
  cairo_surface_flush(argb);

  out->px = malloc((size_t)w * h * 4);
  if (!out->px) { cairo_surface_destroy(argb); return 0; }
  const unsigned char* data = cairo_image_surface_get_data(argb);
  int stride = cairo_image_surface_get_stride(argb);
  for (int y = 0; y < h; y++)
    memcpy(out->px + (size_t)y * w, data + (size_t)y * stride, (size_t)w * 4);
  cairo_surface_destroy(argb);

  out->w = w; out->h = h;
  out->sculpted_yarn = true;
  out->defined_yarn = true;
  snprintf(out->name, sizeof out->name, "%s", name);
  return 1;
}

static int chart_file(const struct dirent* entry) {
  const char* dot = strrchr(entry->d_name, '.');
  return dot && dot != entry->d_name && strcasecmp(dot, ".png") == 0 &&
         (size_t)(dot - entry->d_name) < sizeof g_charts[0].name;
}

static int chart_order(const struct dirent** a, const struct dirent** b) {
  return strcmp((*a)->d_name, (*b)->d_name);
}

int knit_charts_load(const char* dir) {
  g_charts_generation++;
  char active_name[64] = {0};
  if (g_chart_active >= 0 && g_chart_active < g_chart_count)
    snprintf(active_name, sizeof active_name, "%s", g_charts[g_chart_active].name);
  for (int i = 0; i < g_chart_count; i++) free(g_charts[i].px);
  memset(g_charts, 0, sizeof g_charts);
  g_chart_count = 0;
  load_collection();

  // A stable order avoids changing an active chart just because the
  // filesystem returned its directory in a different order.
  struct dirent** entries = NULL;
  int count = dir && dir[0] ? scandir(dir, &entries, chart_file, chart_order) : -1;
  for (int i = 0; i < count; i++) {
    struct dirent* e = entries[i];
    const char* dot = strrchr(e->d_name, '.');
    char path[1400], name[64];
    snprintf(name, sizeof name, "%.*s", (int)(dot - e->d_name), e->d_name);
    int target = knit_chart_index(name);
    int length = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    if (length > 0 && length < (int)sizeof path &&
        (target >= 0 || g_chart_count < KNIT_CHART_MAX)) {
      struct knit_chart chart = {0};
      if (load_one(path, name, &chart)) {
        chart.custom = true;
        if (target < 0) target = g_chart_count++;
        else {
          chart.solid_corners = g_charts[target].solid_corners;
          chart.corner_color = g_charts[target].corner_color;
          chart.cuff_color = g_charts[target].cuff_color;
          chart.round_dots = g_charts[target].round_dots;
          chart.fitted_repeat = g_charts[target].fitted_repeat;
          chart.sculpted_yarn = g_charts[target].sculpted_yarn;
          chart.defined_yarn = g_charts[target].defined_yarn;
          free(g_charts[target].px);
        }
        g_charts[target] = chart;
      }
    }
    free(e);
  }
  free(entries);
  g_chart_active = active_name[0] ? knit_chart_index(active_name) : -1;
  return g_chart_count;
}

int knit_chart_index(const char* name) {
  if (!name) return -1;
  for (int i = 0; i < g_chart_count; i++)
    if (strcmp(g_charts[i].name, name) == 0) return i;
  return -1;
}
