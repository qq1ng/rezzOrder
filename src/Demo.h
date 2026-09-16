#pragma once

#include <cstdint>
#include <string>

#include "Session.h"

// Demo mode: a made-up squad that fights on a loop, so the turn window can be placed, sized and understood
// without waiting for a real squad. The events go through the same session code as a real fight, so what is
// on screen is what the addon would really draw.
namespace Rezz::Demo
{
	// aSelfAccount: the player's own account, so the demo shows their name where theirs would be. May be empty.
	void Start(const std::string& aSelfAccount, uint64_t aNowMs);
	void Stop();
	bool Running();

	// The demo squad as of now; call every frame while Running().
	SessionView View(uint64_t aNowMs);
}
