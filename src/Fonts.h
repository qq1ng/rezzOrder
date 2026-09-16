#pragma once

struct AddonAPI_t;
struct NexusLinkData_t;
struct ImFont;

// Overlay fonts rendered at the overlay's size, so scaling it up stays sharp. They use the same typeface as
// Nexus (its default font or the user's font), copied from Nexus' font atlas. Render thread only.
namespace Fonts
{
	void Init(AddonAPI_t* aApi, NexusLinkData_t* aNexusLink);
	void Shutdown();

	// Requests fonts for this overlay scale; a changed scale is rebuilt once it stops changing.
	void Update(float aScale, unsigned aNowMs);

	// Pushes the overlay font (or scales Nexus' font while ours isn't ready) for text at aScale times Nexus'
	// font size. aBig selects the banner font. Pair with Pop().
	void Push(float aScale, bool aBig = false);
	void Pop();
}
