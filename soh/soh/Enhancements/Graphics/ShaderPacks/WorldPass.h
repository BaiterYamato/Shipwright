#pragma once

struct GraphicsContext;

#ifdef __cplusplus
#include <span>
#include <string>
#include <string_view>

namespace ShaderPacks {
struct PassSource;
bool ActivateWorldPass(std::span<const PassSource> sources, std::string_view name, std::string& error);
void DisableWorldPass();
} // namespace ShaderPacks

extern "C" {
#endif

void ShaderPacks_DrawWorldPass(struct GraphicsContext* gfxCtx);
void ShaderPacks_CaptureWorldGeometry(struct GraphicsContext* gfxCtx, int enable);

#ifdef __cplusplus
}
#endif
