#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "FakeSquad.h"
#include "Session.h"
#include "Settings.h"

// The moment every scenario is rendered at (the render context uses the same one).
// Squads and situations to render. A scenario builds a real Rezz::Session from synthetic arcdps events, so
// the harness draws what the addon would draw in game, not a hand-written view.
namespace Shots
{
	inline constexpr uint64_t kNowMs = 600000;

	// The squad builder lives in the addon itself: demo mode plays with the same one.
	using Player = Rezz::Fake::Player;
	using Squad  = Rezz::Fake::Squad;

	struct Scenario
	{
		std::string Name;
		std::string Note;                       // one line for the contact sheet
		std::function<void(Settings::Values&)> Tweak;  // settings this scenario needs (layout, toggles)
		std::function<Rezz::SessionView()>     Build;
		// Which window the shot is of. Overlay by default.
		// Screen: the whole canvas, for things drawn outside any window (the edge flash).
		enum class Window : uint8_t { Overlay, Editor, Share, Request, Screen };
		Window      Shows = Window::Overlay;
	};

	// Every scenario the harness knows, in the order they are rendered.
	const std::vector<Scenario>& All();
}
