// Window Sweaters for Hyprland: knitted window borders, colours inspired by
// your favourite apps, and a cosier desktop.
#define WLR_USE_UNSTABLE

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <sstream>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/ipc/s1/S1.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "KnitDecoration.hpp"
#include "globals.hpp"

extern "C" {
#include "../core/apps.h"
#include "../core/autoyarn.h"
#include "../core/chart.h"
#include "../core/knit.h"
}

static SP<CEventLoopTimer>           g_pollTimer;
static SP<IPC::Socket1::SCommand>    g_command;

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::vector<std::string> words(const std::string& list) {
    std::vector<std::string> out;
    std::string              word;
    for (char c : list) {
        if (c == ',' || std::isspace((unsigned char)c)) {
            if (!word.empty())
                out.push_back(word);
            word.clear();
        } else
            word += c;
    }
    if (!word.empty())
        out.push_back(word);
    return out;
}

bool sweatersAppExcluded(const std::string& app) {
    const auto& s = g_pGlobalState->applied;
    for (const auto& w : words(s.exclude))
        if (knit_app_matches(app.c_str(), w.c_str()))
            return true;
    const auto include = words(s.include);
    if (include.empty())
        return false;
    return std::ranges::none_of(include, [&](const auto& w) { return knit_app_matches(app.c_str(), w.c_str()); });
}

static void damageAll() {
    for (const auto& d : g_pGlobalState->decorations)
        if (const auto deco = d.lock())
            deco->damageEntire();
}

static void relayoutAll() {
    for (const auto& d : g_pGlobalState->decorations)
        if (const auto deco = d.lock())
            deco->reposition();
    for (const auto& m : State::monitorState()->monitors())
        g_layoutManager->recalculateMonitor(m);
}

static void notifyError(const std::string& error) {
    if (error == g_pGlobalState->lastError)
        return;
    g_pGlobalState->lastError = error;
    if (!error.empty())
        HyprlandAPI::addNotification(PHANDLE, "[window-sweaters] " + error, CHyprColor{1.0, 0.4, 0.3, 1.0}, 6000);
}

// Push the pattern, gauge and colour choices into the renderer.
static void applyToRenderer(const SSweaterSettings& s) {
    const std::string pattern = lower(s.pattern);
    g_knit_stitch             = KNIT_STOCKINETTE;
    if (pattern == "by-app" || pattern == "byapp" || pattern.empty())
        knit_pattern_select("by-app");
    else if (pattern == "stockinette" || pattern == "plain" || pattern == "none")
        knit_pattern_select("none");
    else if (pattern == "rib") {
        g_knit_stitch = KNIT_RIB;
        knit_pattern_select("none");
    } else if (pattern == "garter") {
        g_knit_stitch = KNIT_GARTER;
        knit_pattern_select("none");
    } else if (!knit_pattern_select(s.pattern.c_str())) {
        notifyError(std::format("unknown pattern \"{}\", using by-app. `hyprctl sweaters list` shows them all.", s.pattern));
        knit_pattern_select("by-app");
    }

    // Colourwork needs enough rows to show the whole chart, as on macOS.
    float minimum = 3.F;
    if (g_knit_pattern_by_app)
        minimum = 6.F;
    else if (g_chart_active >= 0 && g_chart_active < g_chart_count)
        minimum = std::max(6.F, (float)g_charts[g_chart_active].h);
    g_knit.rows = std::max(minimum, s.rows);

    const std::string anchor = lower(s.anchor);
    g_knit_anchor            = anchor == "centre" || anchor == "center" ? KNIT_ANCHOR_CENTRE : KNIT_ANCHOR_CORNER;

    g_knit_basket = 3;
    for (int i = 0; i < g_knit_basket_count; i++)
        if (lower(s.basket) == g_knit_baskets[i].name)
            g_knit_basket = i;

    knit_flush_cache();
}

const SSweaterSettings& sweatersRefresh() {
    auto&            st = *g_pGlobalState;
    SSweaterSettings s;
    s.on      = st.overrides.on.value_or(st.config.enabled->value());
    s.width   = std::clamp<float>(st.overrides.width.value_or(st.config.width->value()), 2.F, 64.F);
    s.rows    = std::clamp<float>(st.overrides.rows.value_or(st.config.rows->value()), 1.5F, 32.F);
    s.pattern = st.overrides.pattern.value_or(st.config.pattern->value());
    s.anchor  = st.config.anchor->value();
    s.basket  = st.config.basket->value();
    s.dim     = std::clamp<float>(st.config.dim->value(), 0.F, 1.F);
    s.include = st.config.include->value();
    s.exclude = st.config.exclude->value();

    if (st.appliedOnce && s == st.applied)
        return st.applied;

    const bool relayout = !st.appliedOnce || s.on != st.applied.on || s.width != st.applied.width || s.include != st.applied.include || s.exclude != st.applied.exclude;
    if (!st.appliedOnce || s.pattern != st.applied.pattern)
        notifyError("");
    st.applied     = s;
    st.appliedOnce = true;
    applyToRenderer(s);

    // This can run while Hyprland is positioning decorations; move windows
    // only once it is done.
    g_pEventLoopManager->doLater([relayout] {
        if (!g_pGlobalState)
            return;
        if (relayout)
            relayoutAll();
        damageAll();
    });
    return st.applied;
}

static void onNewWindow(PHLWINDOW window) {
    if (!g_pGlobalState)
        return;
    if (std::ranges::any_of(window->presentation().decorations(), [](const auto& d) { return d->getDisplayName() == "Window Sweaters"; }))
        return;

    auto deco    = makeShared<CKnitDecoration>(window);
    deco->m_self = deco;
    g_pGlobalState->decorations.emplace_back(deco);
    HyprlandAPI::addWindowDecoration(PHANDLE, window, std::move(deco));
}

static void onUpdateWindowRules(PHLWINDOW window) {
    if (!g_pGlobalState)
        return;
    for (const auto& d : g_pGlobalState->decorations)
        if (const auto deco = d.lock(); deco && deco->getOwner() == window)
            deco->updateRules();
}

static void onIconReady(const char* app, void*) {
    for (const auto& d : g_pGlobalState->decorations) {
        const auto deco = d.lock();
        if (!deco)
            continue;
        const auto owner = deco->getOwner();
        if (owner && owner->metadata().appID() == app)
            deco->damageEntire();
    }
}

static void reloadFiles() {
    knit_apps_load();
    knit_charts_load(knit_charts_dir());
    knit_auto_reset();
    g_pGlobalState->appliedOnce = false;
    sweatersRefresh();
}

static std::string statusText() {
    const auto&       s = g_pGlobalState->applied;
    std::stringstream out;
    out << std::format("Window Sweaters: {}\n", s.on ? "on" : "off");
    out << std::format("  pattern: {}\n  width: {:g}\n  rows: {:g}\n", s.pattern, s.width, g_knit.rows);
    out << std::format("  apps.conf: {}\n  charts: {}\n", knit_apps_path(), knit_charts_dir());
    return out.str();
}

static std::string listText() {
    std::stringstream out;
    out << "Patterns (plugin:window-sweaters:pattern, or `hyprctl sweaters pattern NAME`):\n";
    out << "  by-app        each app wears its own sweater\n";
    out << "  stockinette   plain knitting in each app's yarn\n";
    out << "  rib           plain ribbing\n";
    out << "  garter        plain garter ridges\n";
    for (int i = 0; i < g_chart_count; i++) {
        if (g_charts[i].generated)
            continue;
        out << "  " << g_charts[i].name << (g_charts[i].custom ? "  (yours)" : "") << "\n";
    }
    out << "\nApps with their own sweater:\n";
    for (int i = 0; i < knit_collection_count(); i++) {
        const auto* r = knit_collection_rule(i);
        out << std::format("  {:<22} #{:06X} {}\n", r->match, r->color & 0xffffff, r->chart);
    }
    return out.str();
}

static const char* USAGE = "usage: hyprctl sweaters [status | list | toggle | on | off | pattern NAME | width PT | rows N | reload | reset]\n";

// `hyprctl sweaters ...`: the menu bar of the macOS app, for keybinds and scripts.
static std::string onCommand(const std::string& request) {
    auto args = words(request);
    if (!args.empty())
        args.erase(args.begin()); // "sweaters"
    auto& o = g_pGlobalState->overrides;

    if (args.empty() || args[0] == "status")
        return statusText();
    if (args[0] == "list")
        return listText();
    if (args[0] == "toggle")
        o.on = !g_pGlobalState->applied.on;
    else if (args[0] == "on")
        o.on = true;
    else if (args[0] == "off")
        o.on = false;
    else if (args[0] == "reset")
        o = {};
    else if (args[0] == "reload")
        reloadFiles();
    else if (args.size() == 2 && args[0] == "pattern") {
        const auto p = lower(args[1]);
        if (p != "by-app" && p != "stockinette" && p != "plain" && p != "none" && p != "rib" && p != "garter") {
            const int index = knit_chart_index(args[1].c_str());
            if (index < 0 || g_charts[index].generated)
                return std::format("unknown pattern \"{}\"; see `hyprctl sweaters list`\n", args[1]);
        }
        o.pattern = args[1];
    } else if (args.size() == 2 && (args[0] == "width" || args[0] == "rows")) {
        try {
            const float value = std::stof(args[1]);
            if (!std::isfinite(value))
                throw std::invalid_argument("not finite");
            (args[0] == "width" ? o.width : o.rows) = value;
        } catch (...) { return std::format("not a number: {}\n", args[1]); }
    } else
        return USAGE;

    sweatersRefresh();
    damageAll();
    return "ok\n";
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[window-sweaters] Failure in initialization: Version mismatch (headers ver is not equal to running hyprland ver)",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[window-sweaters] Version mismatch");
    }

    g_pGlobalState             = makeUnique<SGlobalState>();
    g_pGlobalState->offRuleIdx = Desktop::Rule::windowEffects()->registerEffect("window-sweaters:off");

    auto& c   = g_pGlobalState->config;
    c.enabled = makeShared<Config::Values::CBoolValue>("plugin:window-sweaters:enabled", "Whether windows wear sweaters", true);
    c.width   = makeShared<Config::Values::CFloatValue>("plugin:window-sweaters:width", "Border width in logical pixels (6, 8, 12, 14, 18, 28)", 12.F,
                                                        Config::Values::SFloatValueOptions{.min = 2.F, .max = 64.F});
    c.rows    = makeShared<Config::Values::CFloatValue>("plugin:window-sweaters:rows", "Stitch rows across the band: 3 chunky, 6 medium, 10 fine, 14 very fine", 6.F,
                                                        Config::Values::SFloatValueOptions{.min = 1.5F, .max = 32.F});
    c.dim     = makeShared<Config::Values::CFloatValue>("plugin:window-sweaters:dim", "How much unfocused sweaters are darkened, 0..1", 0.F,
                                                        Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
    c.pattern = makeShared<Config::Values::CStringValue>("plugin:window-sweaters:pattern", "by-app, zigzag, stockinette, rib, or any chart name", "by-app");
    c.anchor  = makeShared<Config::Values::CStringValue>("plugin:window-sweaters:anchor", "Where pattern repeats start on each side: corner or centre", "corner");
    c.basket  = makeShared<Config::Values::CStringValue>("plugin:window-sweaters:basket", "Fallback yarn basket: wool, sorbet, forest, mono, dopamine, neon, punch", "wool");
    c.include = makeShared<Config::Values::CStringValue>("plugin:window-sweaters:include", "If set, only these app ids wear sweaters (comma separated)", "");
    c.exclude = makeShared<Config::Values::CStringValue>("plugin:window-sweaters:exclude", "App ids that never wear sweaters (comma separated)", "");

    for (const auto& value : std::initializer_list<SP<Config::Values::IValue>>{c.enabled, c.width, c.rows, c.dim, c.pattern, c.anchor, c.basket, c.include, c.exclude})
        HyprlandAPI::addConfigValueV2(PHANDLE, value);

    knit_apps_load();
    knit_charts_load(knit_charts_dir());

    static auto P1 = Event::bus()->m_events.window.open.listen([&](PHLWINDOW w) { onNewWindow(w); });
    static auto P2 = Event::bus()->m_events.window.updateRules.listen([&](PHLWINDOW w) { onUpdateWindowRules(w); });
    static auto P3 = Event::bus()->m_events.window.active.listen([&](PHLWINDOW, Desktop::eFocusReason) {
        // The previously focused window loses its focus without an event of its own.
        if (g_pGlobalState && g_pGlobalState->applied.dim > 0.001F)
            damageAll();
    });
    static auto P4 = Event::bus()->m_events.config.reloaded.listen([&] {
        if (!g_pGlobalState)
            return;
        g_pGlobalState->overrides = {};
        sweatersRefresh();
        for (const auto& d : g_pGlobalState->decorations)
            if (const auto deco = d.lock())
                deco->updateRules();
    });

    g_command = HyprlandAPI::registerHyprCtlCommand(PHANDLE, IPC::Socket1::SCommand{.name    = "sweaters",
                                                                                      .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
                                                                                      .handler = [](const IPC::Socket1::SRequest& request) {
                                                                                          return IPC::Socket1::SResponse{onCommand(request.command)};
                                                                                      }});

    // Icon colours are sampled on a worker thread; hand them over here.
    g_pollTimer = makeShared<CEventLoopTimer>(
        std::chrono::milliseconds(200),
        [](SP<CEventLoopTimer> self, void*) {
            if (g_pGlobalState)
                knit_auto_poll(onIconReady, nullptr);
            self->updateTimeout(std::chrono::milliseconds(200));
        },
        nullptr);
    g_pEventLoopManager->addTimer(g_pollTimer);

    HyprlandAPI::reloadConfig();

    for (auto& w : Desktop::windowState()->windows()) {
        if (w->isHidden() || !validMapped(w))
            continue;
        onNewWindow(w);
    }

    return {"window-sweaters", "Knitted window borders, colours inspired by your favourite apps.", "Window Sweaters", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_pollTimer) {
        g_pEventLoopManager->removeTimer(g_pollTimer);
        g_pollTimer.reset();
    }
    knit_auto_shutdown();

    for (auto& m : State::monitorState()->monitors())
        m->m_scheduledRecalc = true;

    g_pHyprRenderer->context().m_pass.removeAllOfType("CKnitPassElement");

    if (g_pGlobalState)
        Desktop::Rule::windowEffects()->unregisterEffect(g_pGlobalState->offRuleIdx);

    knit_flush_cache();
}
