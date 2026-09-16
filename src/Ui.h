#pragma once

#include <atomic>

#include "Diagnostics.h"

namespace Ui
{
	inline constexpr const char* kWindowName = "Rezz Order - Phase 0 Recorder";

	extern bool                      ShowWindow; // owned by the render thread; hidden by default
	extern Diagnostics::Dependencies Deps;       // owned by the render thread
	extern std::atomic<bool> ToggleRequested; // set from keybind callbacks
	extern std::atomic<bool> MarkRequested;   // set from keybind callbacks
	extern std::atomic<bool> TextInputActive; // true while one of our text fields has keyboard focus

	void Render(bool aIsGameplay);
	void Options();
}
