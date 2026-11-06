// Unit tests for the Cairo core: app matching, apps.conf, charts, icon
// colours and the ring's coverage. Runs in a private XDG sandbox.
#include "../core/knit.h"
#include "../core/chart.h"
#include "../core/apps.h"
#include "../core/autoyarn.h"
#include "../core/paths.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[256];
static uint32_t pixel(cairo_surface_t* s, int x, int y);

static void write_file(const char* path, const char* text) {
  FILE* f = fopen(path, "w"); assert(f); fputs(text, f); fclose(f);
}

static void sandbox(void) {
  snprintf(g_root, sizeof g_root, "/tmp/ws-core-test-XXXXXX");
  assert(mkdtemp(g_root));
  char path[512];
  snprintf(path, sizeof path, "%s/config", g_root); mkdir(path, 0755);
  setenv("XDG_CONFIG_HOME", path, 1);
  snprintf(path, sizeof path, "%s/data", g_root); mkdir(path, 0755);
  setenv("XDG_DATA_HOME", path, 1);
  setenv("XDG_DATA_DIRS", "/nonexistent", 1);
  setenv("HOME", g_root, 1);
}

static const char* rule_chart(const char* app) {
  const struct app_rule* r = knit_app_rule(app);
  return r ? r->chart : "(none)";
}

static void test_matching(void) {
  knit_charts_load(NULL);
  knit_apps_load();
  // Linux app ids reach the macOS collection.
  assert(strcmp(rule_chart("firefox"), "atelier-firefox") == 0);
  assert(strcmp(rule_chart("org.mozilla.firefox"), "atelier-firefox") == 0);
  assert(strcmp(rule_chart("google-chrome"), "atelier-chrome") == 0);
  assert(strcmp(rule_chart("chromium"), "atelier-chrome") == 0);
  assert(strcmp(rule_chart("code"), "atelier-vscode") == 0);
  assert(strcmp(rule_chart("code-oss"), "atelier-vscode") == 0);
  assert(strcmp(rule_chart("Slack"), "atelier-slack") == 0);
  assert(strcmp(rule_chart("com.slack.Slack"), "atelier-slack") == 0);
  assert(strcmp(rule_chart("discord"), "checker") == 0);
  assert(strcmp(rule_chart("com.mitchellh.ghostty"), "atelier-ghostty") == 0);
  assert(strcmp(rule_chart("org.telegram.desktop"), "atelier-telegram") == 0);
  assert(strcmp(rule_chart("org.gnome.Nautilus"), "atelier-finder") == 0);
  assert(strcmp(rule_chart("teams-for-linux"), "atelier-teams") == 0);
  assert(strcmp(rule_chart("cursor"), "atelier-cursor") == 0);
  // Longest prefix wins: Codex is not VS Code.
  assert(strcmp(rule_chart("Codex"), "atelier-chatgpt") == 0);
  // Unknown apps have no rule, and a short id does not match a long rule.
  assert(knit_app_rule("kitty") == NULL);
  assert(knit_app_rule("co") == NULL);
  assert(knit_app_rule("") == NULL && knit_app_rule(NULL) == NULL);
  // Every collection rule names a chart that exists.
  for (int i = 0; i < knit_collection_count(); i++)
    assert(knit_chart_index(knit_collection_rule(i)->chart) >= 0);
  // Fallback colours are stable and from the basket.
  assert(knit_color_for_app("kitty") == knit_color_for_app("KITTY"));
}

static void test_apps_conf(void) {
  const char* path = knit_apps_path();
  assert(path[0]);
  FILE* f = fopen(path, "r"); assert(f); fclose(f);   // seeded
  write_file(path,
    "# comment\n"
    "kitty = #112233 picnic\n"
    "firefox = #445566\n"
    "bad = #12345\n"
    "worse = 123456\n"
    "trailing = #abcdef zigzag extra\n");
  assert(knit_apps_load() == 2);
  const struct app_rule* r = knit_app_rule("kitty");
  assert(r && r->color == 0xff112233u && strcmp(r->chart, "picnic") == 0);
  assert(knit_app_rule_personal(r));
  r = knit_app_rule("firefox");
  assert(r && r->color == 0xff445566u && r->chart[0] == 0 && knit_app_rule_personal(r));
  assert(knit_pattern_for_app("kitty") == knit_chart_index("picnic"));
  assert(knit_pattern_for_app("firefox") == -1);
  assert(!knit_app_rule_personal(knit_app_rule("Slack")));
  write_file(path, "");
  assert(knit_apps_load() == 0);
}

static void test_patterns(void) {
  assert(knit_pattern_select("zigzag"));
  assert(!g_knit_pattern_by_app && knit_zigzag_active());
  assert(knit_pattern_for_app("Slack") == knit_chart_index("zigzag"));
  assert(!knit_pattern_select("no-such-chart"));
  assert(knit_zigzag_active());
  assert(knit_pattern_select("none") && g_chart_active == -1 && !knit_zigzag_active());
  assert(knit_pattern_select("by-app") && g_knit_pattern_by_app);
  // Cream on dark yarn; a deeper shade on pale yarn.
  assert(knit_zigzag_contrast(0xff2855a1u) == KNIT_CREAM);
  uint32_t deep = knit_zigzag_contrast(0xfff5f5f2u);
  assert(deep != KNIT_CREAM && (deep & 0xff) < 0xf2);
}

static void test_user_chart(void) {
  const char* dir = knit_charts_dir();
  assert(dir[0]);
  char path[1200];
  snprintf(path, sizeof path, "%s/hearts.png", dir);
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 2);
  cairo_t* c = cairo_create(s);
  cairo_set_source_rgb(c, 1, 0, 0);
  cairo_rectangle(c, 0, 0, 1, 1); cairo_fill(c);
  cairo_destroy(c);
  assert(cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS);
  snprintf(path, sizeof path, "%s/zigzag.png", dir);
  assert(cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS);
  cairo_surface_destroy(s);
  int builtins = knit_charts_load(NULL);
  assert(knit_pattern_select("twinkle"));
  int total = knit_charts_load(dir);
  assert(total == builtins + 1);
  int hearts = knit_chart_index("hearts");
  assert(hearts >= 0 && g_charts[hearts].w == 4 && g_charts[hearts].h == 2 && g_charts[hearts].custom);
  assert(g_charts[hearts].px[0] == 0xffff0000u && g_charts[hearts].px[1] == 0);
  int zigzag = knit_chart_index("zigzag");
  assert(g_charts[zigzag].custom && g_charts[zigzag].w == 4);
  // The active chart survives a reload by name.
  assert(g_chart_active == knit_chart_index("twinkle"));
  knit_pattern_select("by-app");
  knit_charts_load(NULL);
}

// An icon made of two colours: mostly blue, a quarter orange.
static void make_icon(const char* path) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 128, 128);
  cairo_t* c = cairo_create(s);
  cairo_set_source_rgb(c, 0.1, 0.35, 0.9);
  cairo_paint(c);
  cairo_set_source_rgb(c, 0.95, 0.55, 0.1);
  cairo_rectangle(c, 0, 0, 64, 64); cairo_fill(c);
  cairo_destroy(c);
  assert(cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS);
  cairo_surface_destroy(s);
}

static void ready(const char* app, void* data) {
  (void)app;
  ++*(int*)data;
}

static void test_icons(void) {
  char dir[600], path[900];
  snprintf(dir, sizeof dir, "%s/data/applications", g_root); mkdir(dir, 0755);
  snprintf(path, sizeof path, "%s/org.example.Knitter.desktop", dir);
  write_file(path, "[Desktop Entry]\nName=Knitter\nIcon=knitter-icon\nStartupWMClass=knitter-window\n"
                   "[Desktop Action New]\nIcon=wrong\n");
  snprintf(dir, sizeof dir, "%s/data/icons", g_root); mkdir(dir, 0755);
  strcat(dir, "/hicolor"); mkdir(dir, 0755);
  strcat(dir, "/128x128"); mkdir(dir, 0755);
  strcat(dir, "/apps"); mkdir(dir, 0755);
  snprintf(path, sizeof path, "%s/knitter-icon.png", dir);
  make_icon(path);

  char found[1024];
  assert(knit_icon_path("org.example.Knitter", found, sizeof found) && strcmp(found, path) == 0);
  assert(knit_icon_path("knitter-window", found, sizeof found) && strcmp(found, path) == 0);
  assert(knit_icon_path("Knitter", found, sizeof found) && strcmp(found, path) == 0);
  assert(!knit_icon_path("no-such-app", found, sizeof found));

  uint32_t base = 0, contrast = 0;
  assert(knit_icon_file_colours(path, &base, &contrast));
  // Blue dominates; softened, it stays blue and mid-toned.
  assert((base & 0xff) > ((base >> 16) & 0xff));
  assert(contrast != base);

  // A grey icon has no committed colour.
  uint32_t grey[48 * 48];
  for (int i = 0; i < 48 * 48; i++) grey[i] = 0xff808080u;
  assert(!knit_icon_palette(grey, &base, &contrast));

  // The worker: the first request is pending, the result arrives by polling.
  knit_charts_load(NULL);
  knit_apps_load();
  uint32_t yarn = 0; int chart = -1;
  assert(!knit_auto_yarn("knitter-window", &yarn, &chart));
  int delivered = 0;
  for (int i = 0; i < 500 && knit_auto_poll(ready, &delivered); i++) usleep(2000);
  knit_auto_poll(ready, &delivered);
  assert(delivered == 1);
  assert(knit_auto_yarn("knitter-window", &yarn, &chart));
  assert(knit_icon_file_colours(path, &base, &contrast) && yarn == base);
  assert(chart >= 0 && g_charts[chart].generated);
  // Authored rules always win over icon colours.
  assert(!knit_auto_yarn("Slack", &yarn, &chart));
  // Shared Zigzag: the hand-designed apps take their icon colour too.
  assert(knit_pattern_select("zigzag"));
  uint32_t z = 0xfff5f5f2u; int zc = knit_chart_index("zigzag");
  knit_zigzag_yarn("Notion", &z, &zc);   // pale: needs its own deeper zigzag
  assert(zc >= 0 && g_charts[zc].generated);
  knit_pattern_select("by-app");
  knit_auto_shutdown();

#ifdef KNIT_HAVE_RSVG
  snprintf(path, sizeof path, "%s/scalable.svg", g_root);
  write_file(path, "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'>"
                   "<rect width='64' height='64' fill='#d03030'/></svg>");
  assert(knit_icon_file_colours(path, &base, &contrast));
  assert(((base >> 16) & 0xff) > (base & 0xff));   // red stays red
#endif
}

// The plugin renders the band as four strips (top, bottom, left, right), each
// its own texture. Composited, they must equal one full render, at integer and
// fractional output scales. This mirrors CKnitDecoration::renderPieces.
static void test_pieces(void) {
  knit_charts_load(NULL);
  const double scales[] = { 1.0, 1.25, 1.5, 2.0 };
  const int charts[] = { -1, knit_chart_index("atelier-whatsapp"), knit_chart_index("atelier-notes"),
                         knit_chart_index("atelier-finder"), knit_chart_index("zigzag") };
  for (size_t si = 0; si < sizeof scales / sizeof scales[0]; si++)
    for (size_t ci = 0; ci < sizeof charts / sizeof charts[0]; ci++) {
      double scale = scales[si], lw = 401.3, lh = 233.7, band = 12, radius = 10;
      int W = (int)lround(lw * scale), H = (int)lround(lh * scale);
      double sx = W / lw, sy = H / lh;
      cairo_surface_t* full = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
      cairo_t* c = cairo_create(full);
      cairo_scale(c, sx, sy);
      knit_draw(c, band, band, lw - 2 * band, lh - 2 * band, (float)radius, (float)band, 0xff2986ceu, charts[ci], 0, 1);
      cairo_destroy(c);

      int T = (int)ceil((band + fmax(radius, 1.0) + 2.0) * fmax(sx, sy));
      int rects[4][4] = { {0, 0, W, T}, {0, H - T, W, T}, {0, T, T, H - 2 * T}, {W - T, T, T, H - 2 * T} };
      cairo_surface_t* composed = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
      cairo_t* out = cairo_create(composed);
      for (int i = 0; i < 4; i++) {
        cairo_surface_t* piece = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rects[i][2], rects[i][3]);
        cairo_t* p = cairo_create(piece);
        cairo_translate(p, -rects[i][0], -rects[i][1]);
        cairo_scale(p, sx, sy);
        knit_draw(p, band, band, lw - 2 * band, lh - 2 * band, (float)radius, (float)band, 0xff2986ceu, charts[ci], 0, 1);
        cairo_destroy(p);
        cairo_set_source_surface(out, piece, rects[i][0], rects[i][1]);
        cairo_paint(out);
        cairo_surface_destroy(piece);
      }
      cairo_destroy(out);
      cairo_surface_flush(full); cairo_surface_flush(composed);
      int diffs = 0;
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          uint32_t a = pixel(full, x, y), b = pixel(composed, x, y);
          for (int k = 0; k < 32; k += 8) {
            int d = (int)((a >> k) & 255) - (int)((b >> k) & 255);
            if (d > 2 || d < -2) { diffs++; break; }
          }
        }
      if (diffs) {
        fprintf(stderr, "pieces differ from the full render: scale %g chart %d, %d pixels\n", scale, charts[ci], diffs);
        abort();
      }
      cairo_surface_destroy(full); cairo_surface_destroy(composed);
    }
}

static uint32_t pixel(cairo_surface_t* s, int x, int y) {
  const unsigned char* d = cairo_image_surface_get_data(s);
  return ((const uint32_t*)(d + (size_t)y * cairo_image_surface_get_stride(s)))[x];
}

// Every pixel strictly inside the visible ring is opaque, at every corner, for
// every collection chart and plain knitting, at two scales: no mitre holes.
static void test_coverage(void) {
  knit_charts_load(NULL);
  const float radii[] = { 0, 6, 10, 24 };
  for (int scale = 1; scale <= 2; scale++)
    for (int k = 0; k < 4; k++)
      for (int chart = -1; chart < g_chart_count; chart++) {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 250 * scale, 180 * scale);
        cairo_t* c = cairo_create(s);
        cairo_scale(c, scale, scale);
        knit_draw(c, 30, 30, 180, 110, radii[k], 10, 0xff6ea77bu, chart, 0, 1);
        cairo_destroy(c);
        cairo_surface_flush(s);
        for (int y = 0; y < 180 * scale; y++)
          for (int x = 0; x < 250 * scale; x++) {
            double px = (x + 0.5) / scale, py = (y + 0.5) / scale;
            // inside the outer rounded rect by 1.5 and outside the window by 1.5
            double r = radii[k] + 10;
            double ox = fmax(fmax(20 + r - px, px - (220 - r)), 0), oy = fmax(fmax(20 + r - py, py - (150 - r)), 0);
            bool inside_outer = (px > 21.5 && px < 218.5 && py > 21.5 && py < 148.5)
                                && sqrt(ox * ox + oy * oy) < r - 1.5;
            double ir = radii[k];
            double ix = fmax(fmax(30 + ir - px, px - (210 - ir)), 0), iy = fmax(fmax(30 + ir - py, py - (140 - ir)), 0);
            bool inside_window = px > 28.5 && px < 211.5 && py > 28.5 && py < 141.5
                                 && (ir == 0 || sqrt(ix * ix + iy * iy) < ir + 1.5);
            if (inside_outer && !inside_window && (pixel(s, x, y) >> 24) < 250) {
              fprintf(stderr, "gap: chart %d radius %g scale %d at %g,%g\n", chart, radii[k], scale, px, py);
              abort();
            }
          }
        cairo_surface_destroy(s);
      }
}

static void test_degenerate(void) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
  cairo_t* c = cairo_create(s);
  knit_draw(c, 10, 10, 0, 10, 4, 10, 0xff000000u, -1, 0, 1);
  knit_draw(c, 10, 10, 3, 3, 40, 10, 0xff000000u, 3, 0.5f, 14);
  knit_draw(c, 10, 10, 10, 10, NAN, 10, 0xff000000u, -1, 0, 1);
  knit_draw(c, 10, 10, 10, 10, 4, 0.5f, 0xff000000u, 999, 0, 1);
  for (int stitch = 0; stitch < KNIT_STITCH_COUNT; stitch++) {
    g_knit_stitch = stitch;
    knit_draw(c, 10, 10, 40, 40, 4, 8, 0xff336699u, -1, 0.3f, 1);
  }
  g_knit_stitch = KNIT_STOCKINETTE;
  cairo_destroy(c);
  cairo_surface_destroy(s);
  knit_flush_cache();
}

int main(void) {
  sandbox();
  test_matching();
  test_apps_conf();
  test_patterns();
  test_user_chart();
  test_icons();
  test_coverage();
  test_pieces();
  test_degenerate();
  printf("core tests passed\n");
  return 0;
}
