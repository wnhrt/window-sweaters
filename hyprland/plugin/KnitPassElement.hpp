#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/render/pass/PassElement.hpp>

class CKnitDecoration;

class CKnitPassElement : public IPassElement {
  public:
    struct SKnitData {
        CKnitDecoration* deco  = nullptr;
        CBox             box;          // logical, monitor-relative
        double           scale = 1.0;
        float            a     = 1.F;
    };

    CKnitPassElement(const SKnitData& data_);
    virtual ~CKnitPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    virtual bool                          needsLiveBlur(Render::CRenderContext& ctx) override;
    virtual bool                          needsPrecomputeBlur(Render::CRenderContext& ctx) override;

    virtual const char*                   passName() override {
        return "CKnitPassElement";
    }

    virtual ePassElementType type() override {
        return EK_CUSTOM;
    }

  private:
    SKnitData data;
};
