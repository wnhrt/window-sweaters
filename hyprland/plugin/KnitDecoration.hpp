#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/Texture.hpp>

#include <array>
#include <string>

// A knitted band around one window. It reserves `width` on every side, just
// outside Hyprland's own border, and is drawn under the window.
//
// The band is rendered with the Cairo port of the macOS renderer into four
// textures (top, bottom, left, right strips) and only re-rendered when the
// window's size, scale, yarn or settings change.
class CKnitDecoration : public IHyprWindowDecoration {
  public:
    CKnitDecoration(PHLWINDOW);
    virtual ~CKnitDecoration();

    virtual SDecorationPositioningInfo getPositioningInfo() override;
    virtual void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
    virtual void                       draw(Render::CRenderContext& ctx, PHLMONITOR, float const& a, const Render::SWindowRenderPresentation& presentation) override;
    virtual eDecorationType            getDecorationType() override;
    virtual void                       updateWindow(PHLWINDOW) override;
    virtual void                       damageEntire() override;
    virtual eDecorationLayer           getDecorationLayer() override;
    virtual uint64_t                   getDecorationFlags() override;
    virtual std::string                getDisplayName() override;
    virtual void                       onWindowFocus() override;

    PHLWINDOW                          getOwner();
    // Re-read the window's rules; reposition if the sweater was turned on/off.
    void                               updateRules(bool repositionIfChanged = true);
    // Re-evaluate the extents after a settings change.
    void                               reposition();
    // Reposition on the next event loop turn, outside rendering and layout.
    void                               repositionLater();

    WP<CKnitDecoration>                m_self;

  private:
    struct SPiece {
        SP<Render::ITexture> tex;
        int                  x = 0, y = 0, w = 0, h = 0; // device pixels, relative to the band's box
    };

    struct SKey {
        int      w = 0, h = 0;
        double   scale  = 0;
        float    band   = 0, radius = 0, dim = 0;
        uint32_t color  = 0;
        int      chart  = -1;
        unsigned revision = 0, generation = 0;

        bool     operator==(const SKey&) const = default;
    };

    void         drawPass(Render::CRenderContext& ctx, const CBox& box, float a, double scale);
    bool         wantsSweater();
    void         renderPieces(const SKey& key, double logicalW, double logicalH);
    CBox         assignedBoxGlobal(const Render::SWindowRenderPresentation& presentation);

    PHLWINDOWREF m_window;
    CBox         m_assignedGeometry;
    SBoxExtents  m_extents;
    bool         m_ruleOff = false;
    bool         m_lastWanted = false;
    bool         m_repositionQueued = false;

    SKey                  m_key;
    std::array<SPiece, 4> m_pieces;
    int                   m_pieceCount = 0;

    friend class CKnitPassElement;
};
