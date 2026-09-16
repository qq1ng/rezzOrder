#pragma once

#include <atomic>
#include <string>

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
