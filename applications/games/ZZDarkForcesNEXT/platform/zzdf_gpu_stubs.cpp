/* Link stubs for GPU-renderer symbols referenced by shared TFE code.
 * The ZZ9000 port always selects a software renderer. */
#include <TFE_System/types.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rclassicGPU.h>
#include <TFE_RenderShared/texturePacker.h>

struct SecObject;
struct RSector;

namespace TFE_Jedi
{
	/* camera globals normally defined in the GPU renderer; declared
	   extern by playerCollision.cpp and friends. */
	Vec3f s_cameraPos = { 0 };
	Vec3f s_cameraDir = { 0 };

	namespace RClassic_GPU
	{
		void resetState() {}
		void setupInitCameraAndLights(s32 width, s32 height)
		{ (void)width;(void)height; }
		void changeResolution(s32 width, s32 height)
		{ (void)width;(void)height; }
		void computeCameraTransform(RSector* sector, f32 pitch, f32 yaw,
		                            f32 camX, f32 camY, f32 camZ)
		{ (void)sector;(void)pitch;(void)yaw;(void)camX;(void)camY;(void)camZ; }
		void computeSkyOffsets() {}
	}

	void screenGPU_init() {}
	void screenGPU_destroy() {}
	void screenGPU_beginQuads(u32 width, u32 height) { (void)width;(void)height; }
	void screenGPU_endQuads() {}
	void screenGPU_beginLines(u32 width, u32 height) { (void)width;(void)height; }
	void screenGPU_endLines() {}
	void screenGPU_beginImageQuads(u32 width, u32 height) { (void)width;(void)height; }
	void screenGPU_endImageQuads() {}
	void screenGPU_drawPoint(ScreenRect* rect, s32 x, s32 z, u8 color)
	{ (void)rect;(void)x;(void)z;(void)color; }
	void screenGPU_drawLine(ScreenRect* rect, s32 x0, s32 z0, s32 x1, s32 z1, u8 color)
	{ (void)rect;(void)x0;(void)z0;(void)x1;(void)z1;(void)color; }
	void screenGPU_drawColoredQuad(fixed16_16 x0, fixed16_16 y0, fixed16_16 x1, fixed16_16 y1, u8 color)
	{ (void)x0;(void)y0;(void)x1;(void)y1;(void)color; }
	void screenGPU_blitTextureScaled(TextureData* texture, DrawRect* rect,
		fixed16_16 x0, fixed16_16 y0, fixed16_16 xScale, fixed16_16 yScale,
		u8 lightLevel, JBool forceTransparency)
	{ (void)texture;(void)rect;(void)x0;(void)y0;(void)xScale;(void)yScale;
	  (void)lightLevel;(void)forceTransparency; }
	void screenGPU_blitTextureLit(TextureData* texture, DrawRect* rect,
		s32 x0, s32 y0, u8 lightLevel, JBool forceTransparency)
	{ (void)texture;(void)rect;(void)x0;(void)y0;(void)lightLevel;
	  (void)forceTransparency; }
	void screenGPU_addImageQuad(s32 x0, s32 z0, s32 x1, s32 z1, TextureGpu* texture)
	{ (void)x0;(void)z0;(void)x1;(void)z1;(void)texture; }
	void screenGPU_addImageQuad(s32 x0, s32 z0, s32 x1, s32 z1, f32 u0, f32 u1, TextureGpu* texture)
	{ (void)x0;(void)z0;(void)x1;(void)z1;(void)u0;(void)u1;(void)texture; }
	void screenGPU_setHudTextureCallbacks(s32 count, TextureListCallback* callbacks,
		bool forceAllocation)
	{ (void)count;(void)callbacks;(void)forceAllocation; }
	void screenGPU_setIndexedColors(u32 count, const Vec4f* colors)
	{ (void)count;(void)colors; }

	void texturepacker_reset() {}
	void texturepacker_setIndexStart(s32 colorIndexStart) { (void)colorIndexStart; }
	void texturepacker_setConversionPalette(s32 index, s32 bpp, const u8* input)
	{ (void)index;(void)bpp;(void)input; }
}
