#pragma once
//////////////////////////////////////////////////////////////////////
// Wall
// Dark Forces Derived Renderer - Wall functions
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Memory/allocator.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_DarkForces/time.h>

struct BM_Header
{
	s16 width;
	s16 height;
	s16 uvWidth;
	s16 uvHeight;
	u8  flags;
	u8  logSizeY;
	s16 compressed;
	s32 dataSize;
	u8  pad[12];
};

enum OpacityFlags
{
	OPACITY_TRANS = FLAG_BIT(3),
};

// was BM_SubHeader
#pragma pack(push)
#pragma pack(1)
struct TextureData
{
	u16 width;		// if = 1 then multiple BM in the file
	u16 height;		// EXCEPT if SizeY also = 1, in which case
					// it is a 1x1 BM
	s16 uvWidth;	// portion of texture actually used
	s16 uvHeight;	// portion of texture actually used

	u32 dataSize;	// Data size for compressed BM
					// excluding header and columns starts table
					// If not compressed, DataSize is unused

	u8 logSizeY;	// logSizeY = log2(SizeY)
					// logSizeY = 0 for weapons
	u8 pad1;
	u16 textureId;	// Added for TFE, replaces u8 pad1[3];

	u8* image;		// Image data.
	u32* columns;	// columns will be NULL except when compressed.

	// 4 bytes
	u8 flags;
	u8 compressed; // 0 = not compressed, 1 = compressed (RLE), 2 = compressed (RLE0)
	u8 pad3[2];
};
#pragma pack(pop)

// Animated texture object.
struct AnimatedTexture
{
	s32 count;					// the number of things to iterate through.
	s32 frame;					// the current iteration index.
	Tick delay;					// the delay between iterations, in ticks.
	Tick nextTick;				// the next time this iterates.
	TextureData** frameList;	// the list of things to cycle through.
	TextureData** texPtr;		// iterates through a list every N seconds/ticks.
	TextureData* baseFrame;		// 
	u8* baseData;				// 
};

enum
{
	BM_ANIMATED_TEXTURE = -2,
};

struct MemoryRegion;

namespace TFE_Jedi
{
	void bitmap_setupAnimationTask();

	void bitmap_setAllocator(MemoryRegion* allocator);
	MemoryRegion* bitmap_getAllocator();
	TextureData* bitmap_load(FilePath* filepath, u32 decompress);
	void bitmap_setupAnimatedTexture(TextureData** texture);

	Allocator* bitmap_getAnimatedTextures();

	// Used for tools.
	TextureData* bitmap_loadFromMemory(const u8* data, size_t size, u32 decompress);
}
