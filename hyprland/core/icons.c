// Icon lookup and palette extraction for Linux.
//
// macOS hands over an app's icon directly. On Linux the window's app id leads
// to a .desktop entry, its Icon= key to a file in an icon theme. The palette
// step is the same as src/autoyarn.m's, on the same 48x48 premultiplied ARGB.
#include "autoyarn.h"
#include "knit.h"
#include <cairo.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef KNIT_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

#define PATH_CAP 1024

// ---- colour helpers -------------------------------------------------------

static void rgb2hsl(uint32_t v, double* h, double* s, double* l) {
  double r=((v>>16)&255)/255.0, g=((v>>8)&255)/255.0, b=(v&255)/255.0;
  double mx=fmax(r,fmax(g,b)), mn=fmin(r,fmin(g,b)); *l=(mx+mn)/2; *h=0; *s=0;
  if (mx!=mn) { double d=mx-mn; *s = *l>0.5 ? d/(2-mx-mn) : d/(mx+mn);
    if (mx==r) *h=(g-b)/d+(g<b?6:0); else if (mx==g) *h=(b-r)/d+2; else *h=(r-g)/d+4; *h/=6; }
}
static uint32_t hsl2rgb(double h, double s, double l) {
  double q = l<0.5 ? l*(1+s) : l+s-l*s, p = 2*l-q, t[3]={h+1.0/3,h,h-1.0/3}, o[3];
  for (int i=0;i<3;i++) { double c=t[i]; if(c<0)c+=1; if(c>1)c-=1;
    o[i] = c<1.0/6 ? p+(q-p)*6*c : c<0.5 ? q : c<2.0/3 ? p+(q-p)*(2.0/3-c)*6 : p; }
  return 0xff000000u|((uint32_t)(o[0]*255)<<16)|((uint32_t)(o[1]*255)<<8)|(uint32_t)(o[2]*255);
}
// The hand-picked palette averages ~0.49 saturation; raw icon colour is near
// 1.0, so it is halved and kept mid-toned or the band looks like plastic.
static uint32_t soften(uint32_t v, double lshift) {
  double h,s,l; rgb2hsl(v,&h,&s,&l);
  s = fmin(0.60, fmax(0.22, s*0.48));
  l = fmin(0.72, fmax(0.30, l*0.85+0.10+lshift));
  return hsl2rgb(h,s,l);
}

bool knit_icon_palette(const uint32_t* px, uint32_t* out_base, uint32_t* out_contrast) {
  const int S = 48;
  int counts[4096];
  memset(counts, 0, sizeof counts);
  int kept = 0;
  for (int i = 0; i < S*S; i++) {
    uint32_t v = px[i];
    if (((v>>24)&255) < 217) continue;                       // transparent corners
    // Premultiplied RGB; recover the actual icon colour.
    double alpha = (v >> 24) & 255;
    double r=((v>>16)&255)/alpha, g=((v>>8)&255)/alpha, b=(v&255)/alpha;
    double mx=fmax(r,fmax(g,b)), mn=fmin(r,fmin(g,b));
    double sat = mx<=0 ? 0 : (mx-mn)/mx;
    // Require real colour strength too, or a black-and-white icon reads as navy.
    if (sat < 0.18 || mx < 0.12 || mx - mn < 0.10) continue;   // greys, whites, blacks
    counts[((int)(r*15)<<8)|((int)(g*15)<<4)|(int)(b*15)]++; kept++;
  }
  // ~1.7% of the icon: enough for a small accent on a neutral icon, not so
  // little that a stray highlight counts as a colour.
  if (kept < S*S/60) return false;                            // no committed colour

  uint32_t picked[3]; int np=0;
  for (int pass=0; pass<3 && np<3; pass++) {
    int best=-1,bc=0;
    for (int i=0;i<4096;i++) {
      if (!counts[i]) continue;
      uint32_t col=0xff000000u|((uint32_t)(((i>>8)&15)/15.0*255)<<16)
                  |((uint32_t)(((i>>4)&15)/15.0*255)<<8)|(uint32_t)((i&15)/15.0*255);
      bool far=true;
      double ch,cs_,cl; rgb2hsl(col,&ch,&cs_,&cl);
      for (int j=0;j<np;j++) {
        double ph,ps,pl; rgb2hsl(picked[j],&ph,&ps,&pl);
        double dh=fabs(ch-ph); if (dh>0.5) dh=1-dh;
        if (dh<0.06 && fabs(cl-pl)<0.20) far=false;
      }
      if (far && counts[i]>bc) { bc=counts[i]; best=i; }
    }
    if (best<0) break;
    picked[np++]=0xff000000u|((uint32_t)(((best>>8)&15)/15.0*255)<<16)
                |((uint32_t)(((best>>4)&15)/15.0*255)<<8)|(uint32_t)((best&15)/15.0*255);
  }
  if (!np) return false;

  double bh,bs,bl; rgb2hsl(picked[0],&bh,&bs,&bl);
  int ci=-1;
  for (int j=1;j<np;j++) { double h2,s2,l2; rgb2hsl(picked[j],&h2,&s2,&l2);
    if (fabs(l2-bl) >= 0.20) { ci=j; break; } }
  *out_base = soften(picked[0],0);
  *out_contrast = ci>=0 ? soften(picked[ci],0) : KNIT_CREAM;
  // Softening can erase a contrast that existed in the raw icon. Check the
  // yarns that will actually be rendered, including after lightness clamping.
  double h,s,l, ch,contrast_s,cl;
  rgb2hsl(*out_base,&h,&s,&l);
  rgb2hsl(*out_contrast,&ch,&contrast_s,&cl);
  if (ci < 0 || fabs(l-cl) < 0.20)
    *out_contrast = l < 0.62 ? KNIT_CREAM : hsl2rgb(h,s,l-0.26);
  return true;
}

// ---- decoding -------------------------------------------------------------

static bool ends_with(const char* s, const char* suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

bool knit_icon_file_colours(const char* path, uint32_t* base, uint32_t* contrast) {
  if (!path || !*path) return false;
  const int S = 48;
  cairo_surface_t* target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, S, S);
  if (cairo_surface_status(target) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(target); return false; }
  cairo_t* cr = cairo_create(target);
  bool drawn = false;
  if (ends_with(path, ".png")) {
    cairo_surface_t* img = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
      int w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img);
      if (w > 0 && h > 0) {
        cairo_scale(cr, (double)S / w, (double)S / h);
        cairo_set_source_surface(cr, img, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        drawn = true;
      }
    }
    cairo_surface_destroy(img);
  }
#ifdef KNIT_HAVE_RSVG
  else if (ends_with(path, ".svg")) {
    RsvgHandle* handle = rsvg_handle_new_from_file(path, NULL);
    if (handle) {
      RsvgRectangle viewport = { 0, 0, S, S };
      drawn = rsvg_handle_render_document(handle, cr, &viewport, NULL);
      g_object_unref(handle);
    }
  }
#endif
  cairo_destroy(cr);
  bool ok = false;
  if (drawn) {
    cairo_surface_flush(target);
    const unsigned char* data = cairo_image_surface_get_data(target);
    int stride = cairo_image_surface_get_stride(target);
    uint32_t px[48 * 48];
    for (int y = 0; y < S; y++) memcpy(px + y * S, data + (size_t)y * stride, (size_t)S * 4);
    ok = knit_icon_palette(px, base, contrast);
  }
  cairo_surface_destroy(target);
  return ok;
}

// ---- lookup ---------------------------------------------------------------

// XDG data directories, most personal first.
static int data_dirs(char dirs[][PATH_CAP], int max) {
  int n = 0;
  const char* home = getenv("HOME");
  const char* data_home = getenv("XDG_DATA_HOME");
  if (data_home && *data_home == '/') snprintf(dirs[n++], PATH_CAP, "%s", data_home);
  else if (home && *home) snprintf(dirs[n++], PATH_CAP, "%s/.local/share", home);
  const char* list = getenv("XDG_DATA_DIRS");
  if (!list || !*list) list = "/usr/local/share:/usr/share";
  char copy[4096];
  snprintf(copy, sizeof copy, "%s", list);
  for (char* save = NULL, *dir = strtok_r(copy, ":", &save); dir && n < max; dir = strtok_r(NULL, ":", &save))
    if (*dir == '/') snprintf(dirs[n++], PATH_CAP, "%s", dir);
  // Flatpak apps export their entries here; most sessions list these already.
  if (home && *home && n < max) snprintf(dirs[n++], PATH_CAP, "%s/.local/share/flatpak/exports/share", home);
  if (n < max) snprintf(dirs[n++], PATH_CAP, "/var/lib/flatpak/exports/share");
  return n;
}

static char* trim(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  char* e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) e--;
  *e = 0;
  return s;
}

// Read Icon= and StartupWMClass= from a .desktop file's [Desktop Entry].
static bool desktop_keys(const char* path, char* icon, size_t icon_cap, char* wmclass, size_t wm_cap) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char line[1024];
  bool in_entry = false;
  icon[0] = wmclass[0] = 0;
  while (fgets(line, sizeof line, f)) {
    char* s = trim(line);
    if (*s == '[') { in_entry = strcmp(s, "[Desktop Entry]") == 0; continue; }
    if (!in_entry) continue;
    if (strncmp(s, "Icon=", 5) == 0) snprintf(icon, icon_cap, "%s", trim(s + 5));
    else if (strncmp(s, "StartupWMClass=", 15) == 0) snprintf(wmclass, wm_cap, "%s", trim(s + 15));
  }
  fclose(f);
  return icon[0] != 0;
}

// The desktop entry for an app id: an exact file name first, then a matching
// StartupWMClass, then a reverse-DNS file whose last part is the app id.
static bool find_desktop_icon(const char* app, char* icon, size_t icon_cap) {
  char dirs[16][PATH_CAP];
  int n = data_dirs(dirs, 16);
  char path[PATH_CAP * 2], wmclass[256], found[512];
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < n; i++) {
      char apps[PATH_CAP + 16];
      snprintf(apps, sizeof apps, "%s/applications", dirs[i]);
      DIR* d = opendir(apps);
      if (!d) continue;
      struct dirent* e;
      while ((e = readdir(d))) {
        const char* name = e->d_name;
        size_t len = strlen(name);
        if (len < 9 || strcmp(name + len - 8, ".desktop") != 0) continue;
        size_t stem = len - 8;
        bool candidate = false;
        if (pass == 0) candidate = stem == strlen(app) && strncasecmp(name, app, stem) == 0;
        else if (pass == 2) {
          const char* dot = NULL;
          for (size_t k = 0; k < stem; k++) if (name[k] == '.') dot = name + k;
          candidate = dot && (size_t)(name + stem - dot - 1) == strlen(app)
                      && strncasecmp(dot + 1, app, strlen(app)) == 0;
        } else candidate = true;
        if (!candidate) continue;
        snprintf(path, sizeof path, "%s/%s", apps, name);
        if (!desktop_keys(path, found, sizeof found, wmclass, sizeof wmclass)) continue;
        if (pass == 1 && strcasecmp(wmclass, app) != 0) continue;
        snprintf(icon, icon_cap, "%s", found);
        closedir(d);
        return true;
      }
      closedir(d);
    }
  return false;
}

static bool readable(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool usable_format(const char* path) {
#ifdef KNIT_HAVE_RSVG
  if (ends_with(path, ".svg")) return true;
#endif
  return ends_with(path, ".png");
}

// Look `name` up in one theme folder: the largest useful bitmap first.
static bool theme_lookup(const char* theme_dir, const char* name, char* out, int capacity) {
  static const char* sizes[] = { "256x256", "512x512", "192x192", "128x128", "96x96",
                                 "64x64", "48x48", "256x256@2", "128x128@2" };
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    snprintf(out, capacity, "%s/%s/apps/%s.png", theme_dir, sizes[i], name);
    if (readable(out)) return true;
  }
#ifdef KNIT_HAVE_RSVG
  snprintf(out, capacity, "%s/scalable/apps/%s.svg", theme_dir, name);
  if (readable(out)) return true;
#endif
  return false;
}

static bool resolve_icon(const char* name, char* out, int capacity) {
  if (name[0] == '/') {
    if (!readable(name) || !usable_format(name)) return false;
    snprintf(out, capacity, "%s", name);
    return true;
  }
  char dirs[16][PATH_CAP];
  int n = data_dirs(dirs, 16);
  char roots[18][PATH_CAP + 8];
  int r = 0;
  const char* home = getenv("HOME");
  if (home && *home) snprintf(roots[r++], sizeof roots[0], "%s/.icons", home);
  for (int i = 0; i < n && r < 18; i++) snprintf(roots[r++], sizeof roots[0], "%s/icons", dirs[i]);
  char theme[PATH_CAP * 2];
  // hicolor is where apps install their own icons; other themes may restyle
  // them, so they are only a fallback.
  for (int i = 0; i < r; i++) {
    snprintf(theme, sizeof theme, "%s/hicolor", roots[i]);
    if (theme_lookup(theme, name, out, capacity)) return true;
  }
  for (int i = 0; i < r; i++) {
    DIR* d = opendir(roots[i]);
    if (!d) continue;
    struct dirent* e;
    while ((e = readdir(d))) {
      if (e->d_name[0] == '.' || strcmp(e->d_name, "hicolor") == 0) continue;
      snprintf(theme, sizeof theme, "%s/%s", roots[i], e->d_name);
      if (theme_lookup(theme, name, out, capacity)) { closedir(d); return true; }
    }
    closedir(d);
  }
  for (int i = 0; i < n; i++) {
    snprintf(out, capacity, "%s/pixmaps/%s.png", dirs[i], name);
    if (readable(out)) return true;
#ifdef KNIT_HAVE_RSVG
    snprintf(out, capacity, "%s/pixmaps/%s.svg", dirs[i], name);
    if (readable(out)) return true;
#endif
  }
  return false;
}

bool knit_icon_path(const char* app, char* out, int capacity) {
  if (!app || !*app || !out || capacity <= 0) return false;
  char icon[512];
  if (find_desktop_icon(app, icon, sizeof icon) && resolve_icon(icon, out, capacity)) return true;
  // Many apps name their icon after their app id.
  if (resolve_icon(app, out, capacity)) return true;
  char lower[256];
  snprintf(lower, sizeof lower, "%s", app);
  for (char* p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);
  return strcmp(lower, app) != 0 && resolve_icon(lower, out, capacity);
}
