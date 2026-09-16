#pragma once

#include "Session.h"

struct ID3D11Device;

// Hooks the offscreen harness uses to feed the addon's UI code.
namespace Host
{
	void SetDevice(ID3D11Device* aDevice);
	// The session the UI reads through Live::GetView() this frame.
	void SetView(const Rezz::SessionView& aView);
	// Fonts have to exist before the first frame; aTtfFile may be empty for ImGui's built-in font.
	void BuildFonts(const char* aTtfFile, float aBaseSize);
	void ReleaseIcons();
}
