#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

#include <optional>
#include <string>
#include <vector>

inline HANDLE PHANDLE = nullptr;

class CKnitDecoration;

// What the sweaters should look like right now: the config values, with any
// change made through `hyprctl sweaters ...` on top until the next reload.
struct SSweaterSettings {
    bool        on      = true;
    float       width   = 12.F;
    float       rows    = 6.F;
    std::string pattern = "by-app";
    std::string anchor  = "corner";
    std::string basket  = "wool";
    float       dim     = 0.F;
    std::string include;
    std::string exclude;

    bool        operator==(const SSweaterSettings&) const = default;
};

struct SOverrides {
    std::optional<bool>        on;
    std::optional<float>       width, rows;
    std::optional<std::string> pattern;
};

struct SGlobalState {
    struct {
        SP<Config::Values::CBoolValue>   enabled;
        SP<Config::Values::CFloatValue>  width, rows, dim;
        SP<Config::Values::CStringValue> pattern, anchor, basket, include, exclude;
    } config;

    SOverrides                   overrides;
    SSweaterSettings             applied;
    bool                         appliedOnce = false;
    std::vector<WP<CKnitDecoration>> decorations;
    uint32_t                     offRuleIdx = 0;
    std::string                  lastError;
};

inline UP<SGlobalState> g_pGlobalState;

// Reads the config (and overrides), and pushes any change into the renderer.
// Returns the settings in effect. Cheap when nothing changed.
const SSweaterSettings& sweatersRefresh();

// True when `app` should go without a sweater under the include/exclude lists.
bool sweatersAppExcluded(const std::string& app);
