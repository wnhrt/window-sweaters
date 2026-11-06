// Cairo port of the Window Sweaters knit renderer (src/knit.c).
//
// The algorithms are kept as close to the macOS renderer as possible so that
// both produce the same sweaters. Where CoreGraphics and Cairo differ, the
// comment says how the difference is compensated:
//
//  * CoreGraphics bitmap contexts are y-up, so their pixel rows run opposite to
//    the drawing's y axis. Here the tile's rows run with y. Every per-pixel
//    step that depends on the row direction (the light's y gradient and the
//    fibre grain) is mirrored so it lands on the same stitch as before.
//  * CoreGraphics has quadratic curves; Cairo only cubic ones, so the stitch
//    curves are raised to the exactly equivalent cubic.
//  * A height field drawn in grey with CoreGraphics replaces what is below it;
//    an opaque RGB24 surface does the same in Cairo.
#include "knit.h"
#include "chart.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct knit_gauge g_knit = {
  .rows = 6.f, .aspect = 1.35f, .row_overlap = 0.12f,
  .yarn = 0.48f, .bow = 0.28f, .jitter = 0.035f,
  .ground = 0.98f, .shadow = 0.74f, .light = 1.18f, .ambient = 0.94f, .depth = 1.0f,
  .relief = 0.70f, .sheen = 0.10f,
  .tuck = 1.f,
};

int g_knit_stitch = KNIT_STOCKINETTE;
int g_knit_basket = 3;
int g_knit_anchor = KNIT_ANCHOR_CORNER;

const char* g_knit_stitch_names[] = { "stockinette", "rib", "garter" };

// Baskets of wool. Windows without a colourway are assigned a colour by
// hashing, so a given app keeps its colour and neighbours rarely collide.
static const uint32_t basket_wool[] = {
  0xffd1495b, 0xff4e8098, 0xffedae49, 0xff7c9885, 0xff9b6a8f,
  0xffe8846b, 0xff3f7d6e, 0xffc46a4e, 0xff6d7ba8, 0xffb8935f,
};
static const uint32_t basket_sorbet[] = {
  0xfff08ca4, 0xff7fc6c4, 0xffffc978, 0xffb08ed4, 0xff8fce90,
  0xffff9f7a, 0xff8ab6f0, 0xffe5a3d0,
};
static const uint32_t basket_forest[] = {
  0xff4a6b52, 0xff7d8f5c, 0xff3f6b6e, 0xff8a7248, 0xff5c6b8a,
  0xff6b5344, 0xff2f5e4a,
};
static const uint32_t basket_mono[] = {
  0xff9aa0a6, 0xff7c8288, 0xffb4bac0, 0xff686e74, 0xff8d939a,
};
static const uint32_t basket_dopamine[] = {
  0xffff2d95, 0xff00d9ff, 0xffffd400, 0xff7c3aff, 0xff00e676,
  0xffff6b00, 0xffff1744, 0xff00b8d4,
};
static const uint32_t basket_neon[] = {
  0xfff50057, 0xff00e5ff, 0xffc6ff00, 0xff651fff, 0xff1de9b6, 0xffff9100,
};
static const uint32_t basket_punch[] = {
  0xffe8175d, 0xff0fb9b1, 0xfffec230, 0xff5f27cd, 0xff10ac84, 0xffee5a24,
};

const struct knit_basket g_knit_baskets[] = {
  { "dopamine", basket_dopamine, sizeof(basket_dopamine) / 4 },
  { "neon",     basket_neon,     sizeof(basket_neon)     / 4 },
  { "punch",    basket_punch,    sizeof(basket_punch)    / 4 },
  { "wool",     basket_wool,     sizeof(basket_wool)     / 4 },
  { "sorbet",   basket_sorbet,   sizeof(basket_sorbet)   / 4 },
  { "forest",   basket_forest,   sizeof(basket_forest)   / 4 },
  { "mono",     basket_mono,     sizeof(basket_mono)     / 4 },
};
const int g_knit_basket_count = sizeof(g_knit_baskets) / sizeof(g_knit_baskets[0]);

uint32_t knit_mix(uint32_t h) {
  h ^= h >> 16; h *= 0x85ebca6b;
  h ^= h >> 13; h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

uint32_t knit_color_for_window(uint32_t id) {
  int basket = g_knit_basket >= 0 && g_knit_basket < g_knit_basket_count ? g_knit_basket : 3;
  const struct knit_basket* b = &g_knit_baskets[basket];
  return b->colors[knit_mix(id) % b->len];
}

// Cream shows on every yarn but a pale one; there, a deeper shade of the
// yarn's own hue keeps the zigzag visible (Notion, Chrome's cream, Notes).
uint32_t knit_zigzag_contrast(uint32_t base) {
  double r = ((base >> 16) & 255) / 255.0, g = ((base >> 8) & 255) / 255.0, b = (base & 255) / 255.0;
  double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), l = (mx + mn) / 2, h = 0, s = 0;
  if (l < 0.62) return KNIT_CREAM;
  if (mx != mn) {
    double d = mx - mn;
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h /= 6;
  }
  l -= 0.26;
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, t[3] = {h + 1.0/3, h, h - 1.0/3}, o[3];
  for (int i = 0; i < 3; i++) {
    double c = t[i]; if (c < 0) c += 1; if (c > 1) c -= 1;
    o[i] = c < 1.0/6 ? p + (q - p) * 6 * c : c < 0.5 ? q : c < 2.0/3 ? p + (q - p) * (2.0/3 - c) * 6 : p;
  }
  return 0xff000000u | ((uint32_t)(o[0] * 255) << 16) | ((uint32_t)(o[1] * 255) << 8) | (uint32_t)(o[2] * 255);
}

uint32_t knit_color_for_app(const char* app) {
  // Stable across windows and sessions, with no icon decoding.
  uint32_t hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)(app ? app : ""); *p; p++) {
    unsigned char c = *p;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    hash = (hash ^ c) * 16777619u;
  }
  return knit_color_for_window(hash);
}

static void rgb_from_hex(uint32_t hex, double* r, double* g, double* b) {
  *r = ((hex >> 16) & 0xff) / 255.0;
  *g = ((hex >> 8) & 0xff) / 255.0;
  *b = ((hex >> 0) & 0xff) / 255.0;
}

// deterministic wobble in [0,1), periodic over the tile so it still wraps
static float knit_noise(int a, int b) {
  float s = sinf(a * 12.9898f + b * 78.233f) * 43758.5453f;
  return s - floorf(s);
}

// ---- a tiny path recorder ---------------------------------------------------
// Paths are replayed into several surfaces (colour and height), so they are
// recorded once. Quadratic segments become the equivalent cubic on replay.

struct seg { unsigned char op; float x1, y1, x2, y2; };
struct path { struct seg* s; int n, cap; };
enum { SEG_MOVE, SEG_LINE, SEG_QUAD };

static void path_push(struct path* p, struct seg s) {
  if (p->n == p->cap) {
    int cap = p->cap ? p->cap * 2 : 256;
    struct seg* grown = realloc(p->s, sizeof *grown * (size_t)cap);
    if (!grown) return;
    p->s = grown; p->cap = cap;
  }
  p->s[p->n++] = s;
}
static void path_move(struct path* p, float x, float y) { path_push(p, (struct seg){SEG_MOVE, x, y, 0, 0}); }
static void path_line(struct path* p, float x, float y) { path_push(p, (struct seg){SEG_LINE, x, y, 0, 0}); }
static void path_quad(struct path* p, float cx, float cy, float x, float y) {
  path_push(p, (struct seg){SEG_QUAD, cx, cy, x, y});
}
static void path_free(struct path* p) { free(p->s); p->s = NULL; p->n = p->cap = 0; }

static void path_replay(cairo_t* cr, const struct path* p) {
  double cx = 0, cy = 0;
  for (int i = 0; i < p->n; i++) {
    const struct seg* s = &p->s[i];
    if (s->op == SEG_MOVE) { cairo_move_to(cr, s->x1, s->y1); cx = s->x1; cy = s->y1; }
    else if (s->op == SEG_LINE) { cairo_line_to(cr, s->x1, s->y1); cx = s->x1; cy = s->y1; }
    else {
      double qx = s->x1, qy = s->y1, x = s->x2, y = s->y2;
      cairo_curve_to(cr, cx + 2.0 / 3.0 * (qx - cx), cy + 2.0 / 3.0 * (qy - cy),
                         x + 2.0 / 3.0 * (qx - x),   y + 2.0 / 3.0 * (qy - y), x, y);
      cx = x; cy = y;
    }
  }
}

// Lay `ncols` x `nrows` stitches, plus a two-cell margin so the pattern wraps.
// Each row is one continuous strand, which is both faster to stroke and how
// knitting actually works.
static void knit_lay(struct path* path, int ncols, int nrows,
                     float sw, float sh, float row_step, float dx, float dy) {
  float bx = sw * 0.5f * g_knit.bow;
  float jit = sw * g_knit.jitter;

  if (g_knit_stitch == KNIT_RIB) {
    // k1p1: vertical columns of wool, every other one sitting proud
    float h = (nrows + 4) * row_step;
    for (int i = -2; i <= ncols + 1; i++) {
      float x = i * sw + sw * 0.5f + dx;
      float w = (i & 1) ? sw * 0.16f : 0.f;   // purl columns pull inward
      path_move(path, x + w, -2 * row_step + dy);
      path_line(path, x + w, h + dy);
    }
    return;
  }

  if (g_knit_stitch == KNIT_GARTER) {
    // horizontal ridges: every row a gentle wave, alternating phase
    for (int j = -2; j <= nrows + 1; j++) {
      float y = j * row_step + dy;
      float phase = (j & 1) ? sw * 0.5f : 0.f;
      path_move(path, -2 * sw + dx, y);
      for (int i = -2; i <= ncols + 1; i++) {
        float x = i * sw + phase + dx;
        path_quad(path, x + sw * 0.25f, y - sh * 0.30f, x + sw * 0.5f, y);
        path_quad(path, x + sw * 0.75f, y + sh * 0.30f, x + sw, y);
      }
    }
    return;
  }

  for (int j = -2; j <= nrows + 1; j++) {
    float y = j * row_step + dy;
    float jy = (knit_noise((j + nrows) % nrows, 3) - 0.5f) * jit * 2.f;
    path_move(path, -2 * sw + dx, y + jy);
    for (int i = -2; i <= ncols + 1; i++) {
      float x = i * sw + dx;
      float jx = (knit_noise((i + ncols) % ncols, (j + nrows) % nrows) - 0.5f) * jit * 2.f;
      path_quad(path, x + bx + jx, y + sh * 0.55f + jy, x + sw * 0.5f + jx, y + sh + jy);
      path_quad(path, x + sw - bx + jx, y + sh * 0.55f + jy, x + sw + jx, y + jy);
    }
  }
}

// Internal cache key for single-colour knitted corner patches.
enum { KNIT_PATCH_CHART = -2 };

struct knit_tile {
  float band, rows; int stitch;
  uint32_t color; int chart; unsigned generation;
  cairo_surface_t* img; float w, h;
};

#define KNIT_LAYERS 5

// Lay the ridge profile: the same path stroked from wide+low to narrow+high,
// which is a stepped approximation of a round strand of wool.
static void knit_ridge(cairo_t* h, const struct path* p, float yarn) {
  static const float w[KNIT_LAYERS] = { 1.00f, 0.80f, 0.60f, 0.40f, 0.20f };
  static const float v[KNIT_LAYERS] = { 0.22f, 0.48f, 0.70f, 0.88f, 1.00f };
  for (int i = 0; i < KNIT_LAYERS; i++) {
    cairo_set_source_rgb(h, v[i], v[i], v[i]);
    cairo_set_line_width(h, yarn * w[i]);
    path_replay(h, p);
    cairo_stroke(h);
  }
}

// separable 3-tap blur, wrapping — smooths the stepped ridge into a round one
static void knit_blur(float* a, int w, int hgt, float* tmp) {
  for (int y = 0; y < hgt; y++)
    for (int x = 0; x < w; x++) {
      int l = (x - 1 + w) % w, r = (x + 1) % w;
      tmp[y*w + x] = (a[y*w + l] + 2.f*a[y*w + x] + a[y*w + r]) * 0.25f;
    }
  for (int y = 0; y < hgt; y++)
    for (int x = 0; x < w; x++) {
      int u = (y - 1 + hgt) % hgt, d = (y + 1) % hgt;
      a[y*w + x] = (tmp[u*w + x] + 2.f*tmp[y*w + x] + tmp[d*w + x]) * 0.25f;
    }
}

static cairo_surface_t* knit_surface(int width, int height) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); return NULL; }
  return s;
}

// A compact fabric shader for colourwork. It shades fine yarn directly into
// the cached tile. Chart dimensions stay fixed while each profile chooses its
// stitch density.
static cairo_surface_t* knit_make_fabric(uint32_t color, int chart,
                                         float sw, float row_step, int tw, int th, int scale) {
  const struct knit_chart* ch = &g_charts[chart];
  int width = tw * scale, height = th * scale;
  cairo_surface_t* surface = knit_surface(width, height);
  if (!surface) return NULL;
  cairo_surface_flush(surface);
  unsigned char* data = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  float stitch_scale = ch->defined_yarn ? 2.f : 1.f;
  float relief = fminf(ch->defined_yarn ? 0.30f : 0.25f,
                       fmaxf(0.f, g_knit.relief * (ch->defined_yarn ? 0.32f : 0.20f)));
  for (int y = 0; y < height; y++) {
    float sy = (y + 0.5f) / (scale * row_step);
    float stitch_y = sy / stitch_scale;
    int row = (int)floorf(stitch_y);
    float v = stitch_y - row;
    uint32_t* out = (uint32_t*)(data + (size_t)y * stride);
    for (int x = 0; x < width; x++) {
      float sx = (x + 0.5f) / (scale * sw);
      int column = (int)floorf(sx);
      float stitch_x = sx / stitch_scale;
      int yarn_column = (int)floorf(stitch_x);
      float u = stitch_x - yarn_column;
      // Let the row boundary follow the small stitch head, avoiding a
      // perfectly square pixel-art edge between contrasting yarn colours.
      float color_warp = ch->round_dots ? 0.f : 0.16f * (1.f - 2.f * fabsf(2.f * u - 1.f));
      int color_row = (int)floorf(sy + color_warp);
      color_row = (color_row % (ch->h * 2) + ch->h * 2) % (ch->h * 2);
      uint32_t yarn = ch->px[(size_t)(color_row / 2) * ch->w + (column / 2) % ch->w];
      if (ch->round_dots && (yarn >> 24) >= 128) {
        int cx = (column / 2) % ch->w, cy = color_row / 2;
        bool left = ch->px[(size_t)cy * ch->w + (cx + ch->w - 1) % ch->w] == yarn;
        bool above = ch->px[(size_t)((cy + ch->h - 1) % ch->h) * ch->w + cx] == yarn;
        float dx = sx * 0.5f - floorf(sx * 0.5f) + (left ? 1.f : 0.f) - 1.f;
        float dy = sy * 0.5f - floorf(sy * 0.5f) + (above ? 1.f : 0.f) - 1.f;
        if (dx * dx + dy * dy > 0.94f) yarn = color;
      }
      if ((yarn >> 24) < 128) yarn = color;
      uint32_t seed = knit_mix((uint32_t)yarn_column * 73856093u ^ (uint32_t)row * 19349663u);
      float wobble = ((seed & 255u) / 255.f - 0.5f) * g_knit.jitter;
      // Rounded, nested stitch legs, with no hard highlight or black crevice.
      float leg = 0.42f * (1.f - v) + 0.08f * v * (1.f - v);
      float distance = fabsf(fabsf(u - 0.5f - wobble) - leg);
      float radius = fmaxf(0.10f, g_knit.yarn * 0.5f);
      float ridge = fmaxf(0.f, 1.f - distance * distance / (radius * radius));
      uint32_t grain = knit_mix((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u);
      float fibre = ((grain & 255u) / 255.f - 0.5f) * 0.014f;
      float shade = g_knit.ground - relief * (1.f - ridge)
                    + (g_knit.ambient - 0.94f) + g_knit.sheen * 0.08f * (0.5f - u) + fibre;
      shade = fmaxf(0.f, shade);
      uint32_t r = (uint32_t)fminf(255.f, ((yarn >> 16) & 255u) * shade);
      uint32_t g = (uint32_t)fminf(255.f, ((yarn >> 8) & 255u) * shade);
      uint32_t b = (uint32_t)fminf(255.f, (yarn & 255u) * shade);
      // CoreGraphics wrote this row bottom-up and drew the tile flipped; the
      // two cancel, so in Cairo the sample row is the pixel row.
      out[x] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
  }
  cairo_surface_mark_dirty(surface);
  return surface;
}

static struct knit_tile knit_make_tile(float band, uint32_t color, int chart) {
  const struct knit_chart* ch = chart >= 0 && chart < g_chart_count ? &g_charts[chart] : NULL;
  bool sculpted = chart == KNIT_PATCH_CHART || (ch && ch->sculpted_yarn);
  struct knit_gauge material = g_knit;
  if (sculpted) {
    // Raised yarn material. Geometry/gauge stays independent of the colour chart.
    material.row_overlap = 0.05f; material.yarn = 0.40f;
    material.ground = 0.94f; material.ambient = 0.84f;
    material.relief = 1.65f; material.sheen = 0.28f;
  }
  struct knit_tile t = { band, g_knit.rows, g_knit_stitch, color, chart, g_charts_generation, NULL, 0, 0 };
  float rows = material.rows; if (rows < 1.5f) rows = 1.5f;
  float row_step = band / rows;
  float sh = row_step / (1.f - material.row_overlap);
  float sw = row_step * material.aspect;

  int ncols, nrows;
  if (ch) {
    // One chart is already a complete repeat. Each authored colour cell spans
    // 2x2 sampling units.
    int density = sculpted ? 1 : 2;
    sw /= density; row_step /= density;
    ncols = ch->w * density; nrows = ch->h * density;
    // Very small user charts still need enough pixels for the ridge filter.
    ncols *= (int)fmaxf(1.f, ceilf(8.f / (sw * ncols)));
    nrows *= (int)fmaxf(1.f, ceilf(8.f / (row_step * nrows)));
  } else {
    // Enough variation to feel hand-knit; even counts preserve rib/garter.
    ncols = 8; nrows = 8;
  }
  int tw = (int)lroundf(sw * ncols), th = (int)lroundf(row_step * nrows);
  if (tw < 8) tw = 8;
  if (th < 8) th = 8;
  sw = (float)tw / ncols;
  row_step = (float)th / nrows;
  sh = row_step / (1.f - material.row_overlap);
  float yarn = sw * material.yarn; yarn = fmaxf(yarn, sculpted ? 0.8f : 0.35f);
  // Colourwork always uses stockinette, regardless of the last plain choice.
  if (!ch && !sculpted && g_knit_stitch == KNIT_RIB) yarn = sw * 0.55f;

  int S = row_step < 4.f ? 4 : 2;
  t.w = (float)tw; t.h = (float)th;
  if (ch && !sculpted) { t.img = knit_make_fabric(color, chart, sw, row_step, tw, th, S); return t; }
  int W = tw * S, H = th * S;

  // ---- the paths, one per yarn colour ------------------------------------
  uint32_t cols[16]; struct path paths[16]; int nc = 0;
  memset(paths, 0, sizeof paths);
  if (sculpted) {
    float bx = sw * 0.5f * material.bow;
    for (int j = -2; j <= nrows + 1; j++)
      for (int i = -2; i <= ncols + 1; i++) {
        uint32_t px = 0;
        if (ch) {
          int cx = (i % ch->w + ch->w) % ch->w, cy = (j % ch->h + ch->h) % ch->h;
          px = ch->px[(size_t)cy * ch->w + cx];
        }
        uint32_t yc = (px >> 24) < 128 ? color : (px | 0xff000000u);
        int k = 0; while (k < nc && cols[k] != yc) k++;
        if (k == nc) {
          if (nc == 16) continue;
          cols[nc++] = yc;
        }
        float x = i * sw, y = j * row_step;
        path_move(&paths[k], x, y);
        path_quad(&paths[k], x + bx, y + sh * 0.55f, x + sw * 0.5f, y + sh);
        path_quad(&paths[k], x + sw - bx, y + sh * 0.55f, x + sw, y);
      }
  } else {
    cols[0] = color; nc = 1;
    knit_lay(&paths[0], ncols, nrows, sw, sh, row_step, 0.f, 0.f);
  }

  // ---- flat colour --------------------------------------------------------
  cairo_surface_t* csurf = knit_surface(W, H);
  cairo_surface_t* hsurf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
  cairo_surface_t* osurf = knit_surface(W, H);
  float* hf  = malloc(sizeof(float) * (size_t)W * H);
  float* tmp = malloc(sizeof(float) * (size_t)W * H);
  if (!csurf || !osurf || cairo_surface_status(hsurf) != CAIRO_STATUS_SUCCESS || !hf || !tmp) {
    if (csurf) cairo_surface_destroy(csurf);
    if (osurf) cairo_surface_destroy(osurf);
    cairo_surface_destroy(hsurf);
    free(hf); free(tmp);
    for (int k = 0; k < nc; k++) path_free(&paths[k]);
    return t;
  }
  cairo_t* cc = cairo_create(csurf);
  cairo_scale(cc, S, S);
  double r, g, b;
  rgb_from_hex(color, &r, &g, &b);
  cairo_set_source_rgb(cc, r * material.ground, g * material.ground, b * material.ground);
  cairo_rectangle(cc, 0, 0, tw, th);
  cairo_fill(cc);
  cairo_set_line_cap(cc, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cc, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_width(cc, yarn);
  for (int k = 0; k < nc; k++) {
    rgb_from_hex(cols[k], &r, &g, &b);
    cairo_set_source_rgb(cc, r, g, b);
    path_replay(cc, &paths[k]);
    cairo_stroke(cc);
  }
  cairo_destroy(cc);

  // ---- height -------------------------------------------------------------
  cairo_t* hc = cairo_create(hsurf);
  cairo_scale(hc, S, S);
  cairo_set_source_rgb(hc, 0, 0, 0);
  cairo_paint(hc);
  cairo_set_line_cap(hc, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(hc, CAIRO_LINE_JOIN_ROUND);
  for (int k = 0; k < nc; k++) knit_ridge(hc, &paths[k], yarn);
  cairo_destroy(hc);
  for (int k = 0; k < nc; k++) path_free(&paths[k]);

  cairo_surface_flush(hsurf);
  const unsigned char* hdata = cairo_image_surface_get_data(hsurf);
  int hstride = cairo_image_surface_get_stride(hsurf);
  for (int y = 0; y < H; y++) {
    const uint32_t* row = (const uint32_t*)(hdata + (size_t)y * hstride);
    for (int x = 0; x < W; x++) hf[y*W + x] = (row[x] & 255u) / 255.f;
  }
  cairo_surface_destroy(hsurf);
  knit_blur(hf, W, H, tmp);
  knit_blur(hf, W, H, tmp);
  free(tmp);

  // ---- light it -----------------------------------------------------------
  // from the upper left, the convention that reads as raised rather than sunken
  const float lx = -0.45f, ly = -0.55f, lz = 0.70f;
  float relief = S * material.relief;   // gradient is per-pixel, so scale with S

  cairo_surface_flush(csurf);
  const unsigned char* cdata = cairo_image_surface_get_data(csurf);
  int cstride = cairo_image_surface_get_stride(csurf);
  cairo_surface_flush(osurf);
  unsigned char* odata = cairo_image_surface_get_data(osurf);
  int ostride = cairo_image_surface_get_stride(osurf);
  for (int y = 0; y < H; y++) {
    // Rows here run with y; CoreGraphics' ran against it. Its "next row" was
    // the previous one here, so the y gradient takes the opposite neighbours,
    // and the grain is keyed by the row CoreGraphics would have used.
    int up = (y - 1 + H) % H, dn = (y + 1) % H;
    int grain_row = H - 1 - y;
    const uint32_t* crow = (const uint32_t*)(cdata + (size_t)y * cstride);
    uint32_t* orow = (uint32_t*)(odata + (size_t)y * ostride);
    for (int x = 0; x < W; x++) {
      int lft = (x - 1 + W) % W, rgt = (x + 1) % W;
      float dzdx = (hf[y*W + rgt] - hf[y*W + lft]) * relief;
      float dzdy = (hf[up*W + x]  - hf[dn*W + x])  * relief;
      float nx = -dzdx, ny = -dzdy, nz = 1.f;
      float inv = 1.f / sqrtf(nx*nx + ny*ny + 1.f);
      float ndl = (nx*lx + ny*ly + nz*lz) * inv;
      if (ndl < 0.f) ndl = 0.f;

      // ambient, diffuse, and a little occlusion in the gaps between strands
      float occ = sculpted ? 0.74f + 0.26f * hf[y*W + x]
                           : 0.86f + 0.14f * hf[y*W + x];
      uint32_t grain = knit_mix((uint32_t)x * 73856093u ^ (uint32_t)grain_row * 19349663u);
      float fibre = sculpted ? 1.f : 0.993f + (grain & 255u) * (0.014f / 255.f);
      float shade = (material.ambient + material.sheen * ndl) * occ * fibre;

      uint32_t p = crow[x];
      // Very dark wool still needs a visible strand highlight. Multiplication
      // alone loses almost all relief when a yarn is close to black.
      float peak = fmaxf(p & 255, fmaxf((p >> 8) & 255, (p >> 16) & 255));
      float lift = sculpted ? fmaxf(0.f, 1.f - peak / 48.f) * hf[y*W + x] * 10.f : 0.f;
      float cb = (p & 0xff) * shade + lift;
      float cg = ((p >> 8) & 0xff) * shade + lift;
      float cr = ((p >> 16) & 0xff) * shade + lift;
      if (cr > 255.f) cr = 255.f;
      if (cg > 255.f) cg = 255.f;
      if (cb > 255.f) cb = 255.f;
      orow[x] = 0xff000000u | ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | (uint32_t)cb;
    }
  }
  cairo_surface_mark_dirty(osurf);
  free(hf);
  cairo_surface_destroy(csurf);
  t.img = osurf;
  return t;
}

#define KNIT_CACHE_LEN 64
static struct knit_tile g_cache[KNIT_CACHE_LEN];
static int g_cache_n = 0;

unsigned g_knit_revision;

void knit_flush_cache(void) {
  g_knit_revision++;
  for (int i = 0; i < g_cache_n; i++) cairo_surface_destroy(g_cache[i].img);
  g_cache_n = 0;
}

// The returned tile's image is owned by the caller (one reference).
static struct knit_tile knit_get_tile(float band, uint32_t color, int chart) {
  band = roundf(band * 2.f) / 2.f;
  for (int i = 0; i < g_cache_n; i++) {
    const struct knit_tile* c = &g_cache[i];
    if (c->band == band && c->color == color && c->chart == chart
        && c->rows == g_knit.rows && c->stitch == g_knit_stitch
        && c->generation == g_charts_generation) {
      struct knit_tile t = *c;
      cairo_surface_reference(t.img);
      return t;
    }
  }
  struct knit_tile t = knit_make_tile(band, color, chart);
  if (!t.img) return t;
  if (g_cache_n == KNIT_CACHE_LEN) {              // full: drop the oldest
    cairo_surface_destroy(g_cache[0].img);
    memmove(g_cache, g_cache + 1, sizeof(struct knit_tile) * (KNIT_CACHE_LEN - 1));
    g_cache_n--;
  }
  g_cache[g_cache_n++] = t;
  cairo_surface_reference(t.img);
  return t;
}

// CGContextDrawTiledImage: the tile, scaled to `w` x `h`, repeated from
// (x, y) across the current clip.
static void draw_tiled(cairo_t* cr, cairo_surface_t* img, double x, double y, double w, double h) {
  int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
  if (iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return;
  // CoreGraphics tiles in whole device pixels; so does this, or a fitted
  // repeat drifts against the original over a long edge.
  double dx = w, dy = 0, ex = 0, ey = h;
  cairo_user_to_device_distance(cr, &dx, &dy);
  cairo_user_to_device_distance(cr, &ex, &ey);
  double dw = hypot(dx, dy), dh = hypot(ex, ey);
  if (dw >= 1 && dh >= 1) { w *= round(dw) / dw; h *= round(dh) / dh; }
  cairo_pattern_t* pattern = cairo_pattern_create_for_surface(img);
  cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
  cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);
  cairo_matrix_t m;
  cairo_matrix_init_scale(&m, iw / w, ih / h);
  cairo_matrix_translate(&m, -x, -y);
  cairo_pattern_set_matrix(pattern, &m);
  cairo_set_source(cr, pattern);
  cairo_paint(cr);
  cairo_pattern_destroy(pattern);
}

static void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  if (r > w * 0.5) r = w * 0.5;
  if (r > h * 0.5) r = h * 0.5;
  if (r <= 0) { cairo_rectangle(cr, x, y, w, h); return; }
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_arc(cr, x + r,     y + h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x + r,     y + r,     r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

void knit_draw(cairo_t* cr, double wx, double wy, double ww, double wh, float radius,
               float band, uint32_t color, int chart, float dim, float tuck) {
  if (!cr || !isfinite(band) || !isfinite(radius) || !isfinite(tuck)
      || !isfinite(wx) || !isfinite(wy) || !isfinite(ww) || !isfinite(wh)
      || ww <= 0 || wh <= 0) return;
  if (band < 2.f) band = 2.f;
  radius = fmaxf(0.f, fminf(radius, fminf((float)ww, (float)wh) * 0.5f));
  if (tuck < 1.f) tuck = 1.f;
  // never let the tuck swallow a small window
  float half = (float)(ww < wh ? ww : wh) * 0.5f - 2.f;
  if (tuck > half) tuck = half > 1.f ? half : 1.f;

  double ox = wx - band, oy = wy - band, ow = ww + 2 * band, oh = wh + 2 * band;
  double ix = wx + tuck, iy = wy + tuck, iw = ww - 2 * tuck, ih = wh - 2 * tuck;
  float orad = radius + band;
  float irad = radius > tuck ? radius - tuck : 0.f;

  bool valid = chart >= 0 && chart < g_chart_count;
  bool solid_corners = valid && g_charts[chart].solid_corners;
  uint32_t corner_color = solid_corners && g_charts[chart].corner_color
                            ? g_charts[chart].corner_color : color;
  struct knit_tile t = knit_get_tile(band, color, chart);
  if (!t.img) return;

  cairo_save(cr);
  cairo_matrix_t window_matrix;
  cairo_get_matrix(cr, &window_matrix);

  // clip to the ring — this is what gives correct rounded corners for free
  cairo_new_path(cr);
  rounded_rect(cr, ox, oy, ow, oh, orad);
  rounded_rect(cr, ix, iy, iw, ih, irad);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_clip(cr);

  struct knit_tile cuff_tile = {0};
  bool cuff_ring = false;
  float cuff_width = band / 3.f;
  if (valid && g_charts[chart].cuff_color) {
    cuff_tile = knit_get_tile(band, g_charts[chart].cuff_color, KNIT_PATCH_CHART);
    cuff_ring = cuff_tile.img != NULL;
  }
  struct knit_tile patch = {0};
  if (solid_corners) patch = knit_get_tile(band, corner_color, KNIT_PATCH_CHART);

  // Four separate pieces of knitting, mitred at the corners — the way a frame
  // would actually be made. Each side gets its own local space where +x runs
  // along that edge and +y runs inward. At 45 degrees the inner rounded arc
  // reaches deeper than its straight edge, so the mitre covers that too; the
  // ring clip still defines the exact visible width.
  float d = band + tuck + irad * (1.f - (float)M_SQRT1_2) + 1.f;
  if (d > ow * 0.5f) d = (float)ow * 0.5f;
  if (d > oh * 0.5f) d = (float)oh * 0.5f;

  const struct { double ox, oy, angle, len; } sides[4] = {
    { ox,      oy,      0.0,          ow },
    { ox + ow, oy,      M_PI_2,       oh },
    { ox + ow, oy + oh, M_PI,         ow },
    { ox,      oy + oh, 3 * M_PI_2,   oh },
  };

  bool cuff = valid && strncmp(g_charts[chart].name, "atelier-", 8) == 0;
  bool fitted = valid && g_charts[chart].fitted_repeat;

  for (int i = 0; i < 4; i++) {
    float L = (float)sides[i].len;
    if (L <= 0.f) continue;

    cairo_save(cr);
    cairo_translate(cr, sides[i].ox, sides[i].oy);
    cairo_rotate(cr, sides[i].angle);

    // the mitre: a trapezoid running the length of this edge, its ends cut
    // back at 45 degrees so neighbouring sides meet corner to corner
    cairo_new_path(cr);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, L, 0);
    cairo_line_to(cr, L - d, d);
    cairo_line_to(cr, d, d);
    cairo_close_path(cr);
    cairo_antialias_t aa = cairo_get_antialias(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_clip(cr);
    cairo_set_antialias(cr, aa);

    // Along the side: anchored at the corner (steady under resize) or centred.
    // Across the band the phase depends only on `band`.
    float px = (g_knit_anchor == KNIT_ANCHOR_CENTRE) ? L * 0.5f - t.w * 0.5f : 0.f;
    // The app collection starts with a cuff at the outside edge.
    float py = cuff ? 0.f : band * 0.5f - t.h * 0.5f;
    // Opaque backing also covers image-sampling slivers at tile/curve edges.
    double r, g, b;
    rgb_from_hex(corner_color, &r, &g, &b);
    cairo_set_source_rgb(cr, r * 0.96, g * 0.96, b * 0.96);
    cairo_rectangle(cr, 0, 0, L, d);
    cairo_fill(cr);
    if (patch.img) {
      // Paint only the two small caps, not a second texture under the edge.
      float cap = fminf(orad, L * 0.5f);
      for (int end = 0; end < 2; end++) {
        cairo_save(cr);
        cairo_rectangle(cr, end ? L - cap : 0.f, 0, cap, d);
        cairo_clip(cr);
        draw_tiled(cr, patch.img, 0, py, patch.w, patch.h);
        cairo_restore(cr);
      }
    }
    if (solid_corners) {
      // Keep the curved arc in the base yarn; fit the nearest whole number of
      // repeats between both tangents.
      float cap = fminf(orad, L * 0.5f);
      float available = fmaxf(0.f, L - 2.f * cap);
      if (available > 0.f) {
        float repeats = fmaxf(1.f, roundf(available / t.w));
        float repeat_width = available / repeats;
        cairo_save(cr);
        cairo_rectangle(cr, cap, 0, available, d);
        cairo_clip(cr);
        draw_tiled(cr, t.img, cap, py, repeat_width, t.h);
        cairo_restore(cr);
      }
    } else {
      // Symmetric stripe/check charts can meet at both mitres when each edge
      // contains whole repeats. Other designs retain their existing phase.
      float repeat_width = fitted ? L / fmaxf(1.f, roundf(L / t.w)) : t.w;
      draw_tiled(cr, t.img, fitted ? 0.f : px, py, repeat_width, t.h);
    }

    if (cuff_ring) {
      // Clip the cuff in window coordinates so its width follows the arc,
      // then texture it in the same stitch direction/gauge as this side.
      cairo_save(cr);
      cairo_matrix_t side_matrix;
      cairo_get_matrix(cr, &side_matrix);
      cairo_set_matrix(cr, &window_matrix);
      cairo_new_path(cr);
      rounded_rect(cr, ox, oy, ow, oh, orad);
      rounded_rect(cr, ox + cuff_width, oy + cuff_width, ow - 2 * cuff_width, oh - 2 * cuff_width,
                   orad - cuff_width);
      cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
      cairo_clip(cr);
      cairo_set_matrix(cr, &side_matrix);
      draw_tiled(cr, cuff_tile.img, 0, 0, cuff_tile.w, cuff_tile.h);
      cairo_restore(cr);
    }

    // Unfocused windows are darkened, not faded, so the band stays opaque.
    if (dim > 0.001f) {
      cairo_set_source_rgba(cr, 0, 0, 0, dim);
      cairo_rectangle(cr, -d, -d, L + 2 * d, d + 2 * d);
      cairo_fill(cr);
    }
    cairo_restore(cr);
  }

  cairo_surface_destroy(t.img);
  if (patch.img) cairo_surface_destroy(patch.img);
  if (cuff_tile.img) cairo_surface_destroy(cuff_tile.img);
  cairo_restore(cr);
}
