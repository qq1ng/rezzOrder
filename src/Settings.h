#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// User settings, saved as key=value lines in addons/RezzOrder/settings.txt. Render thread only.
namespace Settings
{
	// Which typeface the overlay draws with. ArcDps: the user's arcdps_font.ttf if they have one, else the
	// font ImGui (and so arcdps) uses by default.
	enum class FontSource : int { Nexus = 0, ArcDps = 1, File = 2 };

	struct Values
	{
		std::vector<std::string> Order;             // account names, in revive order
		std::map<std::string, std::string> Nicknames; // account name -> name to show instead

		bool  OverlayVisible  = true;
		bool  OverlayLocked   = false;          // locked: can't be moved and clicks pass through
		bool  OverlayOnlyWvw  = false;
		float OverlayBgAlpha  = 0.35f;
		float OverlayScale    = 1.0f;
		float OverlayX        = 300.0f;
		float OverlayY        = 300.0f;
		bool        MatchArcDps = true;         // take colours, padding and font size from arcdps.ini
		FontSource  Font = FontSource::ArcDps;
		std::string FontFile;                   // used when Font is File
		bool  OverlayTitleBar   = false;
		bool  OverlayScrollBar  = false;        // fixed size with a scrollbar instead of fitting the content
		bool  OverlayBackground = true;
		float OverlayWidth      = 0.0f;         // 0: as wide as the rows need
		float OverlayHeight     = 160.0f;       // only with the scrollbar
		int   OverlayMaxNameLength = 0;         // 0: full name
		int   OverlayMaxRows       = 0;         // 0: everyone in the order
		bool  PreferAccount   = false;          // always account names, even when the character name is usable
		bool  AlertOnChanges  = true;           // Nexus alert when a player in the order leaves or swaps
		bool  EditorAllProfessions = false;     // editor lists professions without a revive skill too
	};

	extern Values Current;

	void Load(const std::filesystem::path& aFile);
	// Marks settings as changed; they are written by Flush at most once per second.
	void MarkDirty();
	void Flush(unsigned aNowMs, bool aForce = false);
}
