#pragma once
//////////////////////////////////////////////////////////////////////
// Screen Drawing functions using GPU rendering.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include "../screenDraw.h"

namespace TFE_Jedi
{
	void screenGPU_init();
	void screenGPU_destroy();

	void screenGPU_beginLines(u32 width, u32 height);
	void screenGPU_endLines();

	void screenGPU_drawPoint(ScreenRect* rect, s32 x, s32 z, u8 color);
	void screenGPU_drawLine(ScreenRect* rect, s32 x0, s32 z0, s32 x1, s32 z1, u8 color);

	void screenGPU_blitTexture(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, JBool forceTransparency=JFALSE, JBool forceOpaque=JFALSE);
	void screenGPU_blitTextureLit(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, u8 lightLevel, JBool forceTransparency=JFALSE);

	// Scaled versions.
	void screenGPU_blitTextureScaled(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, fixed16_16 xScale, fixed16_16 yScale, JBool forceTransparency = JFALSE);
	void screenGPU_blitTextureLitScaled(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, fixed16_16 xScale, fixed16_16 yScale, u8 lightLevel, JBool forceTransparency = JFALSE);

	void screenGPU_blitTexture(ScreenImage* texture, DrawRect* rect, s32 x0, s32 y0);
	void screenGPU_blitTextureScaled(ScreenImage* texture, DrawRect* rect, s32 x0, s32 y0, fixed16_16 xScale, fixed16_16 yScale);
	void screenGPU_blitTextureLitScaled(ScreenImage* texture, DrawRect* rect, s32 x0, s32 y0, fixed16_16 xScale, fixed16_16 yScale, u8 lightLevel);

	void screenGPU_blitTextureIScale(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, s32 scale);
}