#pragma once
#include "z64math.h"
namespace MMForms {
void DisablePunchQuads();
void SubmitPunchQuad(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d, unsigned damage);
} // namespace MMForms
