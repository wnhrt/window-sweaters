#include "KnitDecoration.hpp"

#include <algorithm>
#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleEffectContainer.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>

#include "KnitPassElement.hpp"
#include "globals.hpp"

using namespace Render::GL;

extern "C" {
#include "../core/apps.h"
#include "../core/autoyarn.h"
#include "../core/chart.h"
#include "../core/knit.h"
}

// The knit tucks this far under the window edge, so no gap can open between
// the band and a rounded corner. Hyprland moves the band in the same frame as
// the window, so the macOS renderer's deeper tuck is not needed.
static constexpr float KNIT_TUCK = 1.F;

CKnitDecoration::CKnitDecoration(PHLWINDOW pWindow) : IHyprWindowDecoration(pWindow), m_window(pWindow) {
    updateRules(false);
}

CKnitDecoration::~CKnitDecoration() {
    damageEntire();
    if (g_pGlobalState)
        std::erase_if(g_pGlobalState->decorations, [this](const auto& d) { return !d || d.get() == this; });
}

PHLWINDOW CKnitDecoration::getOwner() {
    return m_window.lock();
}

void CKnitDecoration::updateRules(bool repositionIfChanged) {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !g_pGlobalState)
        return;

    const bool previous = m_ruleOff;
    m_ruleOff           = false;
    const auto& props   = PWINDOW->m_ruleApplicator->m_otherProps.props;
    if (props.contains(g_pGlobalState->offRuleIdx))
        m_ruleOff = truthy(props.at(g_pGlobalState->offRuleIdx)->effect);

    if (repositionIfChanged && previous != m_ruleOff)
        reposition();
}

bool CKnitDecoration::wantsSweater() {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !g_pGlobalState)
        return false;

    const auto& settings = sweatersRefresh();
    if (!settings.on || m_ruleOff)
        return false;

    if (!PWINDOW->m_ruleApplicator->decorate().valueOrDefault())
        return false;

    if (PWINDOW->m_workspace && Fullscreen::controller()->getFullscreenModes(PWINDOW).internal == Fullscreen::FSMODE_FULLSCREEN)
        return false;

    return !sweatersAppExcluded(PWINDOW->metadata().appID());
}

SDecorationPositioningInfo CKnitDecoration::getPositioningInfo() {
    const bool  WANTED = wantsSweater();
    const float WIDTH  = WANTED ? std::max(2.F, g_pGlobalState->applied.width) : 0.F;
    m_lastWanted       = WANTED;
    m_extents          = {{WIDTH, WIDTH}, {WIDTH, WIDTH}};

    SDecorationPositioningInfo info;
    info.policy         = DECORATION_POSITION_STICKY;
    info.reserved       = true;
    info.priority       = 9985; // just outside Hyprland's own border (10000)
    info.edges          = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;
    info.desiredExtents = m_extents;
    return info;
}

void CKnitDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_assignedGeometry = reply.assignedGeometry;
}

uint64_t CKnitDecoration::getDecorationFlags() {
    // Part of the window: shadows and blur go around the sweater, not inside it.
    return DECORATION_PART_OF_MAIN_WINDOW;
}

eDecorationLayer CKnitDecoration::getDecorationLayer() {
    // Drawn under the window, as on macOS: the window's own edge stays on top.
    return DECORATION_LAYER_UNDER;
}

std::string CKnitDecoration::getDisplayName() {
    return "Window Sweaters";
}

eDecorationType CKnitDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

CBox CKnitDecoration::assignedBoxGlobal(const Render::SWindowRenderPresentation& presentation) {
    CBox box = m_assignedGeometry;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP, m_window));
    return box.translate(presentation.workspaceOffset);
}

void CKnitDecoration::draw(Render::CRenderContext& ctx, PHLMONITOR pMonitor, float const& a, const Render::SWindowRenderPresentation& presentation) {
    if (!validMapped(m_window))
        return;

    const bool WANTED = wantsSweater();
    if (WANTED != m_lastWanted) {
        // Turned on or off since the extents were last handed out.
        repositionLater();
        return;
    }

    if (!WANTED || m_extents.topLeft.x < 1)
        return;

    if (m_assignedGeometry.width < m_extents.topLeft.x * 2 + 1 || m_assignedGeometry.height < m_extents.topLeft.y * 2 + 1)
        return;

    CBox box = assignedBoxGlobal(presentation).translate(-pMonitor->m_position + presentation.floatingOffset);
    if (box.width < 1 || box.height < 1)
        return;

    CKnitPassElement::SKnitData data;
    data.deco  = this;
    data.box   = box;
    data.scale = pMonitor->m_scale;
    data.a     = a;
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CKnitPassElement>(data));
}

// The yarn and chart for an app, decided exactly as the macOS border does.
static void resolveYarn(const std::string& app, uint32_t& yarn, int& chart) {
    const char* name = app.c_str();
    yarn             = knit_color_for_app(name);
    chart            = knit_pattern_for_app(name);
    const auto* rule = knit_app_rule(name);
    if (rule)
        yarn = rule->color;
    if (knit_zigzag_active()) {
        // One shared pattern, every app in its icon's colour.
        knit_zigzag_yarn(name, &yarn, &chart);
    } else if (!rule) {
        // No hand-picked sweater: borrow the app's own colour from its icon.
        uint32_t autoYarn  = 0;
        int      autoChart = -1;
        if (knit_auto_yarn(name, &autoYarn, &autoChart)) {
            yarn = autoYarn;
            if (autoChart >= 0)
                chart = autoChart;
        }
    }
}

void CKnitDecoration::renderPieces(const SKey& key, double logicalW, double logicalH) {
    m_pieceCount = 0;
    for (auto& p : m_pieces)
        p = {};

    const double SX   = key.w / logicalW;
    const double SY   = key.h / logicalH;
    const double BAND = key.band;

    // Only the strips along the edges hold knitting. The corners reach as far
    // in as the window's rounding, so each strip is that much deeper.
    const int T = (int)std::ceil((BAND + std::max<double>(key.radius, KNIT_TUCK) + 2.0) * std::max(SX, SY));

    struct {
        int x, y, w, h;
    } rects[4];
    int count = 0;
    if (key.w <= 2 * T + 1 || key.h <= 2 * T + 1)
        rects[count++] = {0, 0, key.w, key.h};
    else {
        rects[count++] = {0, 0, key.w, T};
        rects[count++] = {0, key.h - T, key.w, T};
        rects[count++] = {0, T, T, key.h - 2 * T};
        rects[count++] = {key.w - T, T, T, key.h - 2 * T};
    }

    for (int i = 0; i < count; i++) {
        const auto& r       = rects[i];
        cairo_surface_t* cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, r.w, r.h);
        if (cairo_surface_status(cs) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(cs);
            continue;
        }
        cairo_t* cr = cairo_create(cs);
        cairo_translate(cr, -r.x, -r.y);
        cairo_scale(cr, SX, SY);
        knit_draw(cr, BAND, BAND, logicalW - 2 * BAND, logicalH - 2 * BAND, key.radius, key.band, key.color, key.chart, key.dim, KNIT_TUCK);
        cairo_destroy(cr);
        cairo_surface_flush(cs);

        auto& piece = m_pieces[m_pieceCount++];
        piece.tex   = g_pHyprRenderer->createTexture(cs);
        piece.x     = r.x;
        piece.y     = r.y;
        piece.w     = r.w;
        piece.h     = r.h;
        cairo_surface_destroy(cs);
    }
}

void CKnitDecoration::drawPass(Render::CRenderContext& ctx, const CBox& box, float a, double scale) {
    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return;

    const auto& settings = g_pGlobalState->applied;
    CBox        devBox   = box.copy().scale(scale).round();
    if (devBox.width < 2 || devBox.height < 2)
        return;

    // The band hugs Hyprland's own border, if there is one.
    const float ROUNDING   = PWINDOW->presentation().rounding();
    const float BORDERSIZE = PWINDOW->presentation().borderSize();

    SKey        key;
    key.w        = (int)devBox.width;
    key.h        = (int)devBox.height;
    key.scale    = scale;
    key.band     = (float)m_extents.topLeft.x; // the width the layout gave this band
    key.radius   = ROUNDING > 0 ? ROUNDING + BORDERSIZE : 0.F;
    key.dim      = Desktop::focusState()->window() == PWINDOW ? 0.F : settings.dim;
    resolveYarn(PWINDOW->metadata().appID(), key.color, key.chart);
    // Read after resolving the yarn: that may rebuild this app's chart.
    key.generation = g_charts_generation;
    key.revision   = g_knit_revision;

    if (!(key == m_key) || m_pieceCount == 0) {
        m_key = key;
        renderPieces(key, box.width, box.height);
    }

    for (int i = 0; i < m_pieceCount; i++) {
        const auto& piece = m_pieces[i];
        if (!piece.tex)
            continue;
        CBox pieceBox = {devBox.x + piece.x, devBox.y + piece.y, (double)piece.w, (double)piece.h};
        g_pHyprOpenGL->renderTexture(ctx, piece.tex, pieceBox, {.a = a});
    }
}

void CKnitDecoration::updateWindow(PHLWINDOW pWindow) {
    damageEntire();
}

void CKnitDecoration::damageEntire() {
    if (!validMapped(m_window))
        return;
    const auto PWINDOW = m_window.lock();
    CBox       box     = assignedBoxGlobal(PWINDOW->presentation().renderPresentation());
    if (box.width <= 0 || box.height <= 0)
        return;
    g_pHyprRenderer->damageBox(box.expand(2));
}

void CKnitDecoration::onWindowFocus() {
    // Only unfocused windows are dimmed, so focus matters only when dimming.
    if (g_pGlobalState && g_pGlobalState->applied.dim > 0.001F)
        damageEntire();
}

void CKnitDecoration::repositionLater() {
    if (m_repositionQueued)
        return;
    m_repositionQueued = true;
    g_pEventLoopManager->doLater([weak = m_self] {
        if (const auto deco = weak.lock()) {
            deco->m_repositionQueued = false;
            deco->reposition();
        }
    });
}

void CKnitDecoration::reposition() {
    damageEntire();
    g_pDecorationPositioner->repositionDeco(this);
    damageEntire();
}
