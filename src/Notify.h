#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

	// The numbers are what the settings file stores, so new sounds go on the end and File keeps its number.
	enum class Sound : int { None = 0, SoftChime, DoubleBeep, RisingBlip, LowThud, Tick, File,
		Bell, TripleBeep, FallingBlip, TwoTone, Pluck, Pulse, Count };

	const char* SoundName(Sound aSound);
	// Every sound in the order a menu lists them: the synthesised ones, then "file..." last.
	const std::vector<Sound>& MenuOrder();

	void Init();
	void Shutdown();
	// Forgets the current standing and any running flash (the render harness, between scenarios).
	void Reset();

	// Plays one, at the volume in the settings. Used by the options page's preview buttons too.
	void Play(Sound aSound);

	// Called every frame with the player's standing; raises the signals when it changes. Returns the banner
	// text to show once, or empty: a banner is an event, and re-sending it every frame would pin it to the
	// screen instead of letting it fade.
	// aInWvw and aGameplay gate everything: no flash on a loading screen or in town. aUnguarded skips the
	// rule that a standing reached again within a few seconds is not announced twice, for the demo squad,
	// where the turn moves on purpose and waiting between tries only gets in the way.
	std::string OnStanding(Standing aStanding, unsigned aNowMs, bool aInWvw, bool aGameplay, bool aUnguarded = false);

	// The sound and flash for a standing, right now, whatever the settings and guards say: the options page's
	// preview, which has to work on every click.
	void Preview(Standing aStanding, unsigned aNowMs);

	// A sound and, if asked, a flash in any colour, right now: for signals that aren't about the turn, like an
	// Illusion of Life starting to run out.
	void Alert(Sound aSound, const float aColor[3], bool aFlash, unsigned aNowMs);

	// Draws the edge flash, if one is running. Render thread, once per frame.
	void Render(unsigned aNowMs, bool aGameplay);

}
