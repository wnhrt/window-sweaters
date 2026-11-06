#pragma once
// Where Window Sweaters keeps its files on Linux:
//   $XDG_CONFIG_HOME/window-sweaters   (default ~/.config/window-sweaters)
#ifdef __cplusplus
extern "C" {
#endif

/// The configuration folder, created on first use. Empty if HOME is unset.
const char* knit_config_dir(void);

#ifdef __cplusplus
}
#endif
