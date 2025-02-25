#pragma once
//////////////////////////////////////////////////////////////////////
// The Force Engine Settings
// This is a global repository for program settings in an INI like
// format.
//
// This includes reading and writing settings as well as storing an
// in-memory cache to get accessed at runtime.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_FileSystem/paths.h>
#include "gameSourceData.h"

struct TFE_Settings_Window
{
	s32 x = 0;
	s32 y = 0;
	u32 width = 1280;
	u32 height = 720;
	u32 baseWidth = 1280;
	u32 baseHeight = 720;
	bool fullscreen = false;
};

struct TFE_Settings_Graphics
{
	Vec2i gameResolution = { 320, 200 };
	bool  widescreen = false;
	bool  asyncFramebuffer = true;
	bool  gpuColorConvert = true;
	bool  colorCorrection = false;
	bool  perspectiveCorrectTexturing = false;
	bool  extendAjoinLimits = true;
	bool  vsync = true;
	f32   brightness = 1.0f;
	f32   contrast = 1.0f;
	f32   saturation = 1.0f;
	f32   gamma = 1.0f;

	// Reticle
	bool reticleEnable  = false;
	s32  reticleIndex   = 6;
	f32  reticleRed     = 0.25f;
	f32  reticleGreen   = 1.00f;
	f32  reticleBlue    = 0.25f;
	f32  reticleOpacity = 1.00f;
	f32  reticleScale   = 1.0f;
};

enum TFE_HudScale
{
	TFE_HUDSCALE_PROPORTIONAL = 0,
	TFE_HUDSCALE_SCALED,
};

enum TFE_HudPosition
{
	TFE_HUDPOS_EDGE = 0,	// Hud elements on the edges of the screen.
	TFE_HUDPOS_4_3,			// Hud elements locked to 4:3 (even in widescreen).
};

static const char* c_tfeHudScaleStrings[] =
{
	"Proportional",		// TFE_HUDSCALE_PROPORTIONAL
	"Scaled",			// TFE_HUDSCALE_SCALED
};

static const char* c_tfeHudPosStrings[] =
{
	"Edge",		// TFE_HUDPOS_EDGE
	"4:3",		// TFE_HUDPOS_4_3
};


struct TFE_Settings_Hud
{
	// Determines whether the HUD stays the same size on screen regardless of resolution or if it gets smaller with higher resolution.
	TFE_HudScale hudScale = TFE_HUDSCALE_PROPORTIONAL;
	// This setting determines how the left and right corners are calculated, which have an offset of (0,0).
	TFE_HudPosition hudPos = TFE_HUDPOS_EDGE;

	// Scale of the HUD, ignored if HudScale is TFE_HUDSCALE_PROPORTIONAL.
	f32 scale = 1.0f;
	// Pixel offset from the left hud corner, right is (-leftX, leftY)
	s32 pixelOffset[2] = { 0 };
};

struct TFE_Settings_Sound
{
	f32 soundFxVolume = 0.75f;
	f32 musicVolume = 1.0f;
	f32 cutsceneSoundFxVolume = 0.9f;
	f32 cutsceneMusicVolume = 1.0f;
	bool use16Channels = false;
};

struct TFE_Game
{
	char game[64] = "Dark Forces";
	GameID id;
};

struct TFE_GameHeader
{
	char gameName[64];
	char sourcePath[TFE_MAX_PATH];
	char emulatorPath[TFE_MAX_PATH];
};

struct TFE_Settings_Game
{
	TFE_GameHeader header[Game_Count];

	// Dark Forces
	s32  df_airControl = 0;				// Air control, default = 0, where 0 = speed/256 and 8 = speed; range = [0, 8]
	bool df_fixBobaFettFireDir = false;	// By default, Boba Fett does not correctly check the angle difference between him and the player in
										// one direction, enabling this will fix that.
	bool df_disableFightMusic = false;	// Set to true to disable fight music and music transitions during gameplay.
};

namespace TFE_Settings
{
	bool init();
	void shutdown();

	bool writeToDisk();

	// Get and set settings.
	TFE_Settings_Window* getWindowSettings();
	TFE_Settings_Graphics* getGraphicsSettings();
	TFE_Settings_Hud* getHudSettings();
	TFE_Settings_Sound* getSoundSettings();
	TFE_Game* getGame();
	TFE_GameHeader* getGameHeader(const char* gameName);
	TFE_Settings_Game* getGameSettings();
}
