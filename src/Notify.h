#pragma once

#include <cstdint>
#include <string>

// Telling the player that the turn has reached them, in three ways they can each switch on or off: the
// Nexus banner, a sound, and a flash along the edges of the screen (the "low health" idea: something the
// eye catches without reading anything).
//
// Sounds are generated as PCM here rather than shipped as files: no assets, no licences, and the volume
// setting is simply the amplitude they are built at.
namespace Notify
{
	// What the player is, as far as the revive order is concerned.
	enum class Standing : uint8_t { None = 0, Up = 1, Backup = 2 };

	enum class Sound : int { None = 0, SoftChime, DoubleBeep, RisingBlip, LowThud, Tick, File, Count };

	const char* SoundName(Sound aSound);

	void Init();
	void Shutdown();
	// Forgets the current standing and any running flash (the render harness, between scenarios).
	void Reset();

	// Plays one, at the volume in the settings. Used by the options page's preview buttons too.
	void Play(Sound aSound);

	// Called every frame with the player's standing; raises the signals when it changes. Returns the banner
	// text to show once, or empty: a banner is an event, and re-sending it every frame would pin it to the
	// screen instead of letting it fade.
	// aInWvw and aGameplay gate everything: no flash on a loading screen or in town.
	std::string OnStanding(Standing aStanding, unsigned aNowMs, bool aInWvw, bool aGameplay);

	// Draws the edge flash, if one is running. Render thread, once per frame.
	void Render(unsigned aNowMs, bool aGameplay);

}
