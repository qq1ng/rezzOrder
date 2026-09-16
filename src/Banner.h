#pragma once

#include <string>

// Messages on screen, drawn by the addon itself. Nexus' own alert takes nothing but the text, so its size, place
// and how long it stays can't be set; players asked for all three.
//
// There are two groups, each with its own size and place on screen: general messages (somebody left, an order
// was shared, a problem) and alerts that ask for action (your turn, backup, an Illusion of Life running out).
// The top middle of the screen is where the game shows the selected target, so either group can be moved away
// from it.
//
// Render thread only.
namespace Banner
{
	// What a message is about, which decides its group and its colour.
	enum class Kind : int { Up, Backup, Info, Problem, Countdown };

	// The countdown's figures and names, against the alert size. The banner font is built at the largest size any
	// banner uses (these figures, usually), so the biggest text stays sharp.
	inline constexpr float kCountdownNumberScale = 1.8f;
	inline constexpr float kCountdownNameScale   = 0.7f;

	// How a message is drawn. Picked in the options; the render harness draws each one side by side.
	enum class Style : int
	{
		Text,     // text with a dark outline and nothing behind it, like the Nexus alert
		Plate,    // a dark rounded plate with a coloured bar down its left edge
		Window,   // a small window in the ImGui / ArcDPS look, titled "Rezz Order"
		Callout,  // large text with a glow in its colour: the loudest of the four
		Count
	};
	const char* StyleName(Style aStyle);

	// Shows a message. The same text again while it is still up only restarts its time instead of stacking.
	void Show(Kind aKind, const std::string& aText, unsigned aNowMs);

	// An Illusion of Life countdown for this frame: the seconds left in large figures, the players' names in
	// smaller ones. Call once per countdown, soonest first, every frame they should show. Several sit side by
	// side at the top of the alerts, centred together.
	void Countdown(unsigned aSeconds, const std::string& aNames);

	// Draws whatever is showing. Once per frame, outside any window.
	void Render(unsigned aNowMs);

	// Drops everything showing (the render harness, between scenarios).
	void Clear();

	// Where the messages were drawn last frame, for the harness to crop a screenshot to. Zero when none.
	struct Area { float X = 0, Y = 0, Width = 0, Height = 0; };
	Area LastArea();
}
