#pragma once
// Per-app colourways.
//
// Some apps deserve their own yarn. Rules live in a plain text file the user
// owns, ~/.config/window-sweaters/apps.conf:
//
//   Claude   = #D58561 atelier-claude
//   firefox  = #643A9A atelier-firefox
//
// The name on the left is matched as a case-insensitive prefix of the window's
// class (its Wayland app id). Spaces, dashes and underscores count as the same
// character, so "Google Chrome" also covers "google-chrome". A reverse-DNS app
// id such as "org.mozilla.firefox" is matched by its last part, too.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KNIT_APP_RULES_MAX 64

struct app_rule {
  char     match[64];
  uint32_t color;
  char     chart[64];   // empty = plain in By App mode
};

extern struct app_rule g_app_rules[KNIT_APP_RULES_MAX];
extern int g_app_rule_count;
extern bool g_knit_pattern_by_app; // true = app profiles; false = global selection

/// Path to apps.conf, created and seeded with a commented example on first run.
const char* knit_apps_path(void);

/// (Re)read apps.conf. Returns the number of user rules loaded.
int knit_apps_load(void);

/// Longest-prefix user match, then built-in app match, or NULL.
const struct app_rule* knit_app_rule(const char* app_name);

/// True for a rule the user wrote in apps.conf, false for the built-in collection.
bool knit_app_rule_personal(const struct app_rule* rule);

/// Resolve the effective chart (-1 = plain), independently of the app's yarn.
/// By App uses the profile, falling back to plain; global mode overrides it.
int knit_pattern_for_app(const char* app_name);

/// Select "by-app", "none" (global plain), or a loaded chart (global pattern).
/// Returns false for an unknown name without changing the current selection.
bool knit_pattern_select(const char* name);

/// The rule matching itself: true if `pattern` is a prefix of `app` (or of the
/// last part of a reverse-DNS app id), ignoring case, with ' ', '-' and '_'
/// counted as the same character.
bool knit_app_matches(const char* app, const char* pattern);

/// Number of built-in collection rules, and each one (for tests and listings).
int knit_collection_count(void);
const struct app_rule* knit_collection_rule(int index);

#ifdef __cplusplus
}
#endif
