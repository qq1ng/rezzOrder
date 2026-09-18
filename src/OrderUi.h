#pragma once

#include <atomic>
#include <string>
#include <vector>

// Revive order UI: the in-combat overlay (who is up, backup, each player's state) and the order editor.
// Render thread only, except the request flags.
namespace OrderUi
{
	inline constexpr const char* kEditorName  = "Rezz Order";
	inline constexpr const char* kOverlayName = "Rezz Order Overlay";

	extern bool              ShowEditor;
	// The fight stats window. Set from the turn window's menu, the Nexus menu, or the summary line.
	extern bool              ShowStats;
	extern std::atomic<bool> TextInputActive; // a text field of ours has keyboard focus
	extern std::atomic<bool> EditorToggleRequested;
	extern std::atomic<bool> LockToggleRequested;
	extern std::atomic<bool> CopyOrderRequested; // put the share line on the clipboard

	// What the turn window drew last frame. Written every frame and read by the offscreen render harness
	// (tools/uishot), which crops its screenshots to the window and diffs the row texts between builds.
	struct FrameInfo
	{
		std::string              Layout;  // "compact", "bars", "focus"
		std::string              Banner;  // the top line, e.g. "YOUR TURN" or "Up: Gorath"
		std::vector<std::string> Rows;    // "4. UP Gorath ready", in drawing order
		float                    X = 0, Y = 0, Width = 0, Height = 0;
		// An open menu, which is its own window and sits outside the one above. Zero when none is open.
		float                    MenuX = 0, MenuY = 0, MenuWidth = 0, MenuHeight = 0;
		// The strip of passing messages against the window's edge, also its own window. Zero when not shown.
		float                    MessagesX = 0, MessagesY = 0, MessagesWidth = 0, MessagesHeight = 0;
		bool                     Drawn = false;
	};

	extern FrameInfo LastFrame;
	extern FrameInfo LastEditorFrame; // the same for the order editor window
	extern FrameInfo LastShareFrame;  // ... and for the "somebody shared an order" prompt
	extern FrameInfo LastRequestFrame; // ... and for the "somebody asked for the order" prompt
	extern FrameInfo LastStatsFrame;   // ... and for the fight stats window

	// Opens a menu by itself so the render harness can photograph it: -1 nothing, -2 the window menu,
	// 0 and up the row menu of that row. Render thread only, and never set while the game is running.
	extern int ShotMenu;

	struct Context
	{
		unsigned NowMs      = 0;
		bool     IsGameplay = true;
		bool     IsMapOpen  = false;
		bool     InWvw      = false;
	};

	// Applies the saved order and backup to the live session.
	void Init();
	void Render(const Context& aContext);
	void Options();
	// A roster notice to show in the overlay for a while.
	// aHighlight is a piece of the text, normally a player's name, drawn dimmer than the rest.
	// aHighlight is a piece of the text, normally a player's name, drawn dimmer than the rest. aOpensStats
	// makes the line clickable, opening the fight stats.
	void AddNotice(const std::string& aText, unsigned aNowMs, const std::string& aHighlight = {}, bool aOpensStats = false);
	// Drops every message at once (the render harness, between scenarios).
	void ClearNotices();
	// We left the squad: forget the saved order too.
	void OnOrderCleared();
}
