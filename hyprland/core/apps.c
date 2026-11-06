// Per-app colourways (port of src/apps.c), matched against Wayland app ids.
#include "apps.h"
#include "chart.h"
#include "paths.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct app_rule g_app_rules[KNIT_APP_RULES_MAX];
int g_app_rule_count = 0;
bool g_knit_pattern_by_app = true;

// The collection stays available even when apps.conf is absent or unreadable.
// User rules are evaluated first, so personal colourways always take priority.
// These are the macOS app names; most Linux app ids match them directly
// (firefox, google-chrome, code, slack, discord, spotify, ...).
static const struct app_rule k_collection[] = {
  { "Finder",          0xff258de0u, "atelier-finder" },
  { "Microsoft Teams", 0xff9283c1u, "atelier-teams" },
  { "MSTeams",         0xff9283c1u, "atelier-teams" },
  { "Teams",           0xff9283c1u, "atelier-teams" },
  { "Claude",          0xffd58561u, "atelier-claude" },
  { "Codex",           0xff0a84ffu, "atelier-chatgpt" },
  { "Spotify",         0xff497641u, "atelier-spotify" },
  { "Notion",          0xfff5f5f2u, "atelier-notion" },
  { "WhatsApp",        0xff6ea77bu, "atelier-whatsapp" },
  { "Figma",           0xffad7197u, "atelier-figma" },
  { "Google Chrome",   0xfff4f0e6u, "atelier-chrome" },
  { "Chrome",          0xfff4f0e6u, "atelier-chrome" },
  { "Paper",           0xff83ade8u, "atelier-paper" },
  { "Chromium",        0xfff4f0e6u, "atelier-chrome" },
  { "Safari", 0xff268ed8u, "atelier-safari" },
  { "Firefox", 0xff643a9au, "atelier-firefox" },
  { "Cursor", 0xff26251eu, "atelier-cursor" },
  { "Slack", 0xff542a52u, "atelier-slack" },
  { "zoom.us", 0xff2877ebu, "atelier-zoom" },
  { "Zoom", 0xff2877ebu, "atelier-zoom" },
  { "Telegram", 0xff389eceu, "atelier-telegram" },
  { "Messages", 0xff55af51u, "atelier-messages" },
  { "Mail", 0xff2986ceu, "atelier-mail" },
  { "Notes", 0xfff6f0d9u, "atelier-notes" },
  { "Calendar", 0xfff7f3e9u, "atelier-calendar" },
  { "Reminders", 0xfff6f3ebu, "atelier-reminders" },
  { "Music", 0xffe64e70u, "atelier-music" },
  { "Apple Music", 0xffe64e70u, "atelier-music" },
  { "Photos", 0xfff8f0dbu, "atelier-photos" },
  { "Preview", 0xff597bafu, "atelier-preview" },
  { "Microsoft Word", 0xff2855a1u, "atelier-word" },
  { "Microsoft Excel", 0xff28674fu, "atelier-excel" },
  { "Microsoft PowerPoint", 0xffb9573du, "atelier-powerpoint" },
  { "Microsoft Outlook", 0xff176bb7u, "atelier-outlook" },
  { "Code", 0xff237cafu, "atelier-vscode" },
  { "Visual Studio Code", 0xff237cafu, "atelier-vscode" },
  { "Adobe Photoshop", 0xff182f45u, "atelier-photoshop" },
  { "Adobe Illustrator", 0xff4e3029u, "atelier-illustrator" },
  { "ChatGPT", 0xff0a84ffu, "atelier-chatgpt" },
  { "Grok Bot", 0xff34383bu, "atelier-grok" },
  { "Discord", 0xff5865f2u, "checker" },
  { "Granola", 0xff292e2au, "atelier-granola" },
  { "Terminal", 0xff303c35u, "atelier-terminal" },
  { "ghostty", 0xff2b3350u, "atelier-ghostty" },
};

// Linux app ids that do not start with their macOS app's name. Each borrows
// that app's colourway. Matching is the same as for any other rule.
static const struct { const char* match; const char* wears; } k_linux_aliases[] = {
  { "org.gnome.Nautilus",     "Finder" },     // file managers wear Finder's checks
  { "Nautilus",               "Finder" },
  { "org.kde.dolphin",        "Finder" },
  { "thunar",                 "Finder" },
  { "nemo",                   "Finder" },
  { "pcmanfm",                "Finder" },
  { "org.gnome.Terminal",     "Terminal" },
  { "gnome-terminal",         "Terminal" },
  { "org.gnome.Ptyxis",       "Terminal" },
  { "org.telegram.desktop",   "Telegram" },
  { "com.spotify.Client",     "Spotify" },
  { "com.mitchellh.ghostty",  "ghostty" },
  { "teams-for-linux",        "Microsoft Teams" },
  { "VSCodium",               "Visual Studio Code" },
  { "codium",                 "Visual Studio Code" },
};

static const char* k_default_conf =
  "# Window Sweaters — per-app colourways\n"
  "#\n"
  "# <app id> = #RRGGBB [chart]\n"
  "#\n"
  "# The name is matched as a case-insensitive prefix of the window's class\n"
  "# (see `hyprctl clients`). Spaces, dashes and underscores count as the\n"
  "# same, and a reverse-DNS class like org.mozilla.firefox also matches by\n"
  "# its last part. The optional chart name is a built-in chart or any file\n"
  "# in the charts folder, without .png.\n"
  "# Personal rules take priority over the built-in app collection.\n"
  "#\n"
  "# Edit, then run `hyprctl dispatch sweaters:reload` to load your changes.\n"
  "\n"
  "# 37 apps already have their own yarn and chart; see docs/COLLECTION.md.\n"
  "# Uncomment to customize:\n"
  "# Claude = #D58561 atelier-claude\n";

const char* knit_apps_path(void) {
  static char path[1100];
  if (path[0]) return path;
  const char* dir = knit_config_dir();
  if (!dir[0]) return "";
  snprintf(path, sizeof path, "%s/apps.conf", dir);
  FILE* f = fopen(path, "r");
  if (f) { fclose(f); return path; }
  f = fopen(path, "w");                     // seed it the first time
  if (f) { fputs(k_default_conf, f); fclose(f); }
  return path;
}

// trim leading and trailing whitespace in place
static char* trim(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
  *e = 0;
  return s;
}

int knit_apps_load(void) {
  g_app_rule_count = 0;
  const char* path = knit_apps_path();
  FILE* f = path[0] ? fopen(path, "r") : NULL;
  if (!f) return 0;

  char line[512];
  while (fgets(line, sizeof line, f) && g_app_rule_count < KNIT_APP_RULES_MAX) {
    // Reject an overlong rule as a whole rather than parsing its fragments.
    if (!strchr(line, '\n') && !feof(f)) {
      int ch;
      while ((ch = fgetc(f)) != '\n' && ch != EOF) {}
      continue;
    }
    char* s = trim(line);
    if (!*s || *s == '#') continue;
    char* eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char* name = trim(s);
    char* val  = trim(eq + 1);
    if (!*name || strlen(name) >= sizeof g_app_rules[0].match || *val != '#') continue;

    // Require exactly six hex digits followed by whitespace or end of line.
    size_t len = strlen(val);
    if (len < 7 || (val[7] && !isspace((unsigned char)val[7]))) continue;
    int valid = 1;
    for (int i = 1; i <= 6; i++)
      if (!isxdigit((unsigned char)val[i])) valid = 0;
    if (!valid) continue;
    unsigned long rgb = strtoul(val + 1, NULL, 16);
    char chart[64] = {0};
    char* tail = trim(val + 7);
    if (*tail && *tail != '#') {
      size_t n = strcspn(tail, " \t\r\n");
      if (n >= sizeof chart) continue;
      memcpy(chart, tail, n);
      tail = trim(tail + n);
      if (*tail && *tail != '#') continue;
    }

    struct app_rule* r = &g_app_rules[g_app_rule_count++];
    snprintf(r->match, sizeof r->match, "%s", name);
    r->color = 0xff000000u | (uint32_t)rgb;
    snprintf(r->chart, sizeof r->chart, "%s", chart);
  }
  fclose(f);
  return g_app_rule_count;
}

static int fold(unsigned char c) {
  if (c == '-' || c == '_') return ' ';
  return tolower(c);
}

// Case-insensitive prefix test where ' ', '-' and '_' are the same character.
static bool prefix_match(const char* name, const char* prefix) {
  if (!*prefix) return false;
  for (; *prefix; name++, prefix++)
    if (!*name || fold((unsigned char)*name) != fold((unsigned char)*prefix)) return false;
  return true;
}

// The app id itself, and for a reverse-DNS id (two dots or more) its last part.
static int candidates(const char* app, const char* out[2]) {
  int n = 0;
  out[n++] = app;
  const char* first = strchr(app, '.');
  const char* last = strrchr(app, '.');
  if (first && last != first && last[1]) out[n++] = last + 1;
  return n;
}

static const struct app_rule* best_in(const struct app_rule* rules, int count,
                                      const char* app, size_t* best_len) {
  const char* names[2];
  int n = candidates(app, names);
  const struct app_rule* best = NULL;
  for (int c = 0; c < n; c++)
    for (int i = 0; i < count; i++) {
      size_t len = strlen(rules[i].match);
      if (len > *best_len && prefix_match(names[c], rules[i].match)) {
        best = &rules[i];
        *best_len = len;                    // longest prefix wins
      }
    }
  return best;
}

static const struct app_rule* collection_named(const char* match) {
  for (size_t i = 0; i < sizeof k_collection / sizeof k_collection[0]; i++)
    if (strcmp(k_collection[i].match, match) == 0) return &k_collection[i];
  return NULL;
}

const struct app_rule* knit_app_rule(const char* app_name) {
  if (!app_name || !*app_name) return NULL;
  size_t best_len = 0;
  const struct app_rule* best = best_in(g_app_rules, g_app_rule_count, app_name, &best_len);
  if (best) return best;
  best = best_in(k_collection, (int)(sizeof k_collection / sizeof k_collection[0]), app_name, &best_len);
  const char* names[2];
  int n = candidates(app_name, names);
  for (int c = 0; c < n; c++)
    for (size_t i = 0; i < sizeof k_linux_aliases / sizeof k_linux_aliases[0]; i++) {
      size_t len = strlen(k_linux_aliases[i].match);
      if (len > best_len && prefix_match(names[c], k_linux_aliases[i].match)) {
        const struct app_rule* rule = collection_named(k_linux_aliases[i].wears);
        if (rule) { best = rule; best_len = len; }
      }
    }
  return best;
}

bool knit_app_rule_personal(const struct app_rule* rule) {
  // Equality only: ordering pointers into two different arrays is undefined.
  for (int i = 0; i < g_app_rule_count; i++) if (rule == &g_app_rules[i]) return true;
  return false;
}

int knit_pattern_for_app(const char* app_name) {
  if (!g_knit_pattern_by_app)
    return g_chart_active >= 0 && g_chart_active < g_chart_count ? g_chart_active : -1;
  const struct app_rule* rule = knit_app_rule(app_name);
  return rule && rule->chart[0] ? knit_chart_index(rule->chart) : -1;
}

bool knit_pattern_select(const char* name) {
  if (!name) return false;
  if (strcmp(name, "by-app") == 0) {
    g_knit_pattern_by_app = true;
    return true;
  }
  int index = strcmp(name, "none") == 0 ? -1 : knit_chart_index(name);
  if (index < 0 && strcmp(name, "none") != 0) return false;
  if (index >= 0 && g_charts[index].generated) return false;
  g_chart_active = index;
  g_knit_pattern_by_app = false;
  return true;
}

int knit_collection_count(void) { return (int)(sizeof k_collection / sizeof k_collection[0]); }
const struct app_rule* knit_collection_rule(int index) {
  return index >= 0 && index < knit_collection_count() ? &k_collection[index] : NULL;
}

bool knit_app_matches(const char* app, const char* pattern) {
  if (!app || !pattern || !*app || !*pattern) return false;
  const char* names[2];
  int n = candidates(app, names);
  for (int c = 0; c < n; c++) if (prefix_match(names[c], pattern)) return true;
  return false;
}
