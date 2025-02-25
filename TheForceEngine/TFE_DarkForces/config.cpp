#include "config.h"
#include <TFE_Game/igame.h>
#include <assert.h>

namespace TFE_DarkForces
{
	GameConfig s_config =
	{
		JTRUE,	// headwave
		JTRUE,	// wpnAutoMount
		JFALSE, // mouseTurnEnabled
		JTRUE,  // mouseLookEnabled
		JFALSE, // superShield
	};

	void config_startup()
	{
		// Read Config
	}

	void config_shutdown()
	{
		// Write Config
	}
}  // TFE_DarkForces