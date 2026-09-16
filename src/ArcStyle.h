#pragma once

#include "imgui/imgui.h"

// Reads the look of the user's ArcDPS windows from arcdps.ini (Appearance tab) so this addon's windows can
// match them: ImGui colours, window padding/rounding/border and the font size. Render thread only.
namespace ArcStyle
{
	struct Values
	{
		bool   Ok = false;
		ImVec4 Colors[ImGuiCol_COUNT] = {};
		bool   HasColor[ImGuiCol_COUNT] = {};
		float  FontSize        = 0.0f; // arcdps [session] font_size
		ImVec2 WindowPadding   = ImVec2(4, 4);
		float  WindowRounding  = 0.0f;
		float  WindowBorder    = 0.0f;
		bool   HasStyle        = false;
	};

	// Re-reads arcdps.ini when it changed (at most every few seconds).
	void Update(unsigned aNowMs);
	const Values& Get();
	// Where arcdps.ini was found, for the options page.
	const char* SourcePath();

	// Applies the arcdps colours (and padding/rounding) until Pop(). Does nothing when arcdps.ini wasn't
	// read or the user turned matching off. Not nestable.
	void Push();
	void Pop();
}
