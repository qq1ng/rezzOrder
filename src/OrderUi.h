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
	extern std::atomic<bool> TextInputActive; // a text field of ours has keyboard focus
	extern std::atomic<bool> EditorToggleRequested;
	extern std::atomic<bool> LockToggleRequested;
	extern std::atomic<bool> CopyOrderRequested; // put the share line on the clipboard
	// Raises a Nexus banner. Set by the addon entry point; the UI itself knows nothing about Nexus.
	extern void (*StandingBanner)(const std::string& aText);

	// What the turn window drew last frame. Written every frame and read by the offscreen render harness
	// (tools/uishot), which crops its screenshots to the window and diffs the row texts between builds.
	struct FrameInfo
	{
		std::string              Layout;  // "compact", "bars", "focus"
		std::string              Banner;  // the top line, e.g. "YOUR TURN" or "Up: Gorath"
		std::vector<std::string> Rows;    // "4. UP Gorath ready", in drawing order
		float                    X = 0, Y = 0, Width = 0, Height = 0;
		bool                     Drawn = false;
	};

	extern FrameInfo LastFrame;
	extern FrameInfo LastEditorFrame; // the same for the order editor window
	extern FrameInfo LastShareFrame;  // ... and for the "somebody shared an order" prompt
	extern FrameInfo LastRequestFrame; // ... and for the "somebody asked for the order" prompt

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
	void AddNotice(const std::string& aText, unsigned aNowMs);
	// We left the squad: forget the saved order too.
	void OnOrderCleared();
}
