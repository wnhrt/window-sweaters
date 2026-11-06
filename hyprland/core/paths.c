#include "paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

const char* knit_config_dir(void) {
  static char dir[1024];
  if (dir[0]) return dir;
  const char* xdg = getenv("XDG_CONFIG_HOME");
  const char* home = getenv("HOME");
  char base[900];
  if (xdg && *xdg == '/') snprintf(base, sizeof base, "%s", xdg);
  else if (home && *home) snprintf(base, sizeof base, "%s/.config", home);
  else return "";
  mkdir(base, 0755);
  snprintf(dir, sizeof dir, "%s/window-sweaters", base);
  mkdir(dir, 0755);
  return dir;
}
