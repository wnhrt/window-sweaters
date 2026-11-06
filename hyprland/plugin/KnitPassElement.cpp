#include "KnitPassElement.hpp"
#include "KnitDecoration.hpp"

CKnitPassElement::CKnitPassElement(const CKnitPassElement::SKnitData& data_) : data(data_) {
    ;
}

std::vector<UP<IPassElement>> CKnitPassElement::draw(Render::CRenderContext& ctx) {
    data.deco->drawPass(ctx, data.box, data.a, data.scale);
    return {};
}

bool CKnitPassElement::needsLiveBlur(Render::CRenderContext& ctx) {
    return false;
}

bool CKnitPassElement::needsPrecomputeBlur(Render::CRenderContext& ctx) {
    return false;
}
