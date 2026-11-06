// Icon-derived colourways (port of src/autoyarn.m).
//
// The cache, the charts and the callbacks belong to the caller's thread. Only
// icon lookup, decoding and palette extraction run on the worker, which never
// touches renderer state: it fills in a job, and knit_auto_poll() hands the
// finished jobs back.
#include "autoyarn.h"
#include "apps.h"
#include "chart.h"
#include "knit.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KNIT_AUTO_MAX 64

struct auto_yarn {
  char app[64];
  uint32_t base, contrast;
  int chart;             // one generated chart per app, for the current mode
  int chart_kind;        // which mode that chart was built for
  uint32_t chart_yarn;   // and in which contrast yarn
  unsigned generation;
  uint64_t request, used;
  bool pending, ok, dirty;
};
static struct auto_yarn g_auto[KNIT_AUTO_MAX];
static int g_auto_count;
static uint64_t g_auto_clock;

enum { CHART_NONE, CHART_BY_APP, CHART_ZIGZAG };

// ---- the worker -------------------------------------------------------------

struct job {
  struct job* next;
  int slot;
  uint64_t request;
  char app[64];
  bool ok;
  uint32_t base, contrast;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static struct job* g_todo;       // newest first; the worker takes the oldest
static struct job* g_done;
static int g_outstanding;        // jobs not yet handed back by knit_auto_poll
static bool g_running, g_stopping;
static pthread_t g_thread;

static void* worker(void* unused) {
  (void)unused;
  pthread_mutex_lock(&g_lock);
  for (;;) {
    while (!g_todo && !g_stopping) pthread_cond_wait(&g_wake, &g_lock);
    if (g_stopping) break;
    struct job** oldest = &g_todo;
    while ((*oldest)->next) oldest = &(*oldest)->next;
    struct job* job = *oldest;
    *oldest = NULL;
    pthread_mutex_unlock(&g_lock);

    char path[1024];
    job->ok = knit_icon_path(job->app, path, sizeof path)
              && knit_icon_file_colours(path, &job->base, &job->contrast);

    pthread_mutex_lock(&g_lock);
    job->next = g_done;
    g_done = job;
  }
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

static void sample_icon(int slot) {
  struct auto_yarn* entry = &g_auto[slot];
  struct job* job = calloc(1, sizeof *job);
  if (!job) return;
  entry->pending = true;
  job->slot = slot;
  job->request = entry->request;
  snprintf(job->app, sizeof job->app, "%s", entry->app);
  pthread_mutex_lock(&g_lock);
  if (!g_running) {
    g_stopping = false;
    g_running = pthread_create(&g_thread, NULL, worker, NULL) == 0;
  }
  if (!g_running) {
    pthread_mutex_unlock(&g_lock);
    free(job);
    entry->pending = false;
    return;
  }
  job->next = g_todo;
  g_todo = job;
  g_outstanding++;
  pthread_cond_signal(&g_wake);
  pthread_mutex_unlock(&g_lock);
}

bool knit_auto_poll(void (*ready)(const char* app, void* data), void* data) {
  pthread_mutex_lock(&g_lock);
  struct job* done = g_done;
  g_done = NULL;
  pthread_mutex_unlock(&g_lock);
  int handed = 0;
  while (done) {
    struct job* job = done;
    done = job->next;
    handed++;
    struct auto_yarn* current = &g_auto[job->slot];
    if (job->slot < g_auto_count && current->request == job->request) {
      current->pending = false;
      current->ok = job->ok;
      current->dirty = job->ok;
      current->base = job->base;
      current->contrast = job->contrast;
      if (job->ok && ready) ready(current->app, data);
    }
    free(job);
  }
  pthread_mutex_lock(&g_lock);
  g_outstanding -= handed;
  bool outstanding = g_outstanding > 0;
  pthread_mutex_unlock(&g_lock);
  return outstanding;
}

void knit_auto_shutdown(void) {
  pthread_mutex_lock(&g_lock);
  bool running = g_running;
  g_stopping = true;
  pthread_cond_broadcast(&g_wake);
  pthread_mutex_unlock(&g_lock);
  if (running) pthread_join(g_thread, NULL);
  pthread_mutex_lock(&g_lock);
  g_running = false;
  while (g_todo) { struct job* j = g_todo; g_todo = j->next; free(j); }
  while (g_done) { struct job* j = g_done; g_done = j->next; free(j); }
  g_outstanding = 0;
  pthread_mutex_unlock(&g_lock);
}

void knit_auto_reset(void) {
  // Pending work still arrives, but no longer matches any request.
  for (int i = 0; i < g_auto_count; i++) g_auto[i].request = 0;
  g_auto_count = 0;
}

// ---- charts -----------------------------------------------------------------

// Clone a built-in motif knitted in one contrast yarn. Each app owns at most
// one generated chart, rebuilt in place when its mode or yarn changes, so the
// chart table stays bounded by the cache.
static int build_chart(struct auto_yarn* entry, const char* motif, int kind, uint32_t yarn) {
  int target = entry->generation == g_charts_generation ? entry->chart : -1;
  if (target < 0 || target >= g_chart_count || !g_charts[target].generated)
    target = -1;
  if (target < 0 && g_chart_count >= KNIT_CHART_MAX) return -1;
  int source = knit_chart_index(motif);
  if (source < 0) return -1;
  struct knit_chart chart = g_charts[source];
  if (!chart.px || chart.w <= 0 || chart.h <= 0) return -1;
  uint32_t* pixels = malloc(sizeof(uint32_t) * chart.w * chart.h);
  if (!pixels) return -1;
  for (int k = 0; k < chart.w * chart.h; k++)
    pixels[k] = (chart.px[k] >> 24) >= 128 ? yarn : 0;
  chart.px = pixels;
  chart.generated = true;
  chart.custom = false;
  snprintf(chart.name, sizeof chart.name, "auto-%llu", (unsigned long long)entry->request);
  if (chart.corner_color) chart.corner_color = yarn;
  if (chart.cuff_color) chart.cuff_color = yarn;
  if (target < 0) target = g_chart_count++;
  else { knit_flush_cache(); free(g_charts[target].px); }
  g_charts[target] = chart;
  entry->chart_kind = kind;
  entry->chart_yarn = yarn;
  return target;
}

// The By App motif is a stable hash of the app name, independent of colour.
static const char* by_app_motif(const char* app) {
  static const char* lean[] = { "zigzag", "picnic", "twinkle" };
  unsigned hash = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)app; *p; p++)
    hash = (hash ^ *p) * 16777619u;
  return lean[hash % 3];
}

// Find or start the icon work for this app. Returns NULL only for unusable
// input or a cache full of in-flight work. The entry may still be pending.
static struct auto_yarn* auto_entry(const char* app) {
  if (!app || !*app || strlen(app) >= sizeof g_auto[0].app) return NULL;
  int slot = -1;
  for (int i = 0; i < g_auto_count; i++)
    if (strcmp(g_auto[i].app, app) == 0) { slot = i; break; }
  if (slot < 0) {
    if (g_auto_count < KNIT_AUTO_MAX) slot = g_auto_count++;
    else {
      // Bound memory without permanently refusing the 65th app. Never evict
      // in-flight requests; jobs carry a token so old work cannot win.
      for (int i = 0; i < g_auto_count; i++)
        if (!g_auto[i].pending && (slot < 0 || g_auto[i].used < g_auto[slot].used)) slot = i;
      if (slot < 0) return NULL;
    }
    struct auto_yarn* entry = &g_auto[slot];
    // A recycled slot keeps its chart, to be rebuilt for the new app.
    int previous = entry->generation == g_charts_generation ? entry->chart : -1;
    *entry = (struct auto_yarn){ .chart = previous, .generation = g_charts_generation,
                                 .request = ++g_auto_clock };
    snprintf(entry->app, sizeof entry->app, "%s", app);
    entry->used = ++g_auto_clock;
    sample_icon(slot);
    return entry;
  }
  struct auto_yarn* entry = &g_auto[slot];
  entry->used = ++g_auto_clock;
  return entry;
}

// The chart this entry needs, rebuilt only when the mode, the yarn or the
// chart table has changed since it was last built.
static int entry_chart(struct auto_yarn* entry, const char* motif, int kind, uint32_t yarn) {
  if (entry->generation != g_charts_generation || entry->chart < 0 || entry->dirty
      || entry->chart_kind != kind || entry->chart_yarn != yarn) {
    entry->chart = build_chart(entry, motif, kind, yarn);
    entry->generation = g_charts_generation;
    entry->dirty = false;
  }
  return entry->chart;
}

bool knit_auto_yarn(const char* app, uint32_t* yarn, int* chart) {
  if (!yarn || !chart || knit_app_rule(app)) return false;
  struct auto_yarn* entry = auto_entry(app);
  if (!entry || entry->pending || !entry->ok) return false;
  *yarn = entry->base;
  *chart = g_knit_pattern_by_app
         ? entry_chart(entry, by_app_motif(entry->app), CHART_BY_APP, entry->contrast)
         : -1;
  return true;
}

bool knit_zigzag_active(void) {
  return !g_knit_pattern_by_app && g_chart_active >= 0
         && g_chart_active == knit_chart_index("zigzag");
}

bool knit_zigzag_yarn(const char* app, uint32_t* yarn, int* chart) {
  if (!yarn || !chart) return false;
  // A colour the user chose in apps.conf is theirs in every pattern; only the
  // built-in collection gives way to icon colours here.
  const struct app_rule* rule = knit_app_rule(app);
  bool personal = rule && knit_app_rule_personal(rule);
  struct auto_yarn* entry = personal ? NULL : auto_entry(app);
  bool changed = false;
  if (entry && !entry->pending && entry->ok) { *yarn = entry->base; changed = true; }
  // A zigzag the user drew themselves is shown exactly as drawn, for every app.
  if (*chart >= 0 && *chart < g_chart_count && g_charts[*chart].custom) return changed;
  uint32_t contrast = knit_zigzag_contrast(*yarn);
  if (contrast == KNIT_CREAM) return changed;    // the shared zigzag is already cream
  if (!entry) entry = auto_entry(app);           // a personal colour still needs a chart slot
  // No room for this app's own zigzag: plain knitting beats cream on cream.
  *chart = entry ? entry_chart(entry, "zigzag", CHART_ZIGZAG, contrast) : -1;
  return true;
}
