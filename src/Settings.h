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

	// How the turn window draws the order. Compact: one text line per player. Bars: a tall bar per player,
	// the recharge filling it. Focus: whoever is up on a big card, the rest as a queue below it. Strip: the
	// order left to right, for a thin bar along an edge of the screen.
	// NextUp: only what has to be acted on - the player who is up, the backup, and the next few, rolling
	// with the turn so the top of the list is always the one to watch.
	enum class OverlayLayout : int { Compact = 0, Bars = 1, Focus = 2, Strip = 3, NextUp = 4 };

	struct Values
	{
		std::vector<std::string> Order;             // account names, in revive order
		// Players who may spend their revive before their turn, when they see a fight going badly. They are
		// in the order like anyone else; the window and the shared line just say so.
		std::vector<std::string> Precast;
		std::map<std::string, std::string> Nicknames; // account name -> name to show instead
		// Named orders, for squads that run together often. Saved as preset=<name>=<account>><account>...
		std::map<std::string, std::vector<std::string>> Presets;

		OverlayLayout Layout  = OverlayLayout::Compact;
		bool  CooldownBar     = true;           // recharge drawn as a filling bar
		bool  CooldownSeconds = true;           // recharge written out in seconds
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
		bool  OverlayBackground = true;
		float OverlayWidth      = 0.0f;         // narrowest the window may be; 0: as wide as the rows need
		float OverlayFilledWidth  = 0.0f;       // the window's size the last time it showed an order, kept while
		float OverlayFilledHeight = 0.0f;       // ... there is none so it still fills its place among other windows
		int   OverlayMaxNameLength = 0;         // 0: full name
		int   OverlayMaxRows       = 0;         // 0: everyone in the order
		bool  PreferAccount   = false;          // always account names, even when the character name is usable
		bool  ShareFromLeaders = true;          // take over an order shared by the commander or a lieutenant
		bool  ShareFromAnyone  = false;         // ... or by anyone in the squad, without asking
		// Who is asked to answer "!rezz?": 0 nobody, 1 only when the order is ours (default), 2 always.
		int   AnswerRequests   = 1;

		// --- Nexus banners, one switch per kind of message. The turn window shows all of them anyway; these
		// decide which are worth interrupting the screen for.
		bool  BannerOnLeave  = false;           // a player in the order leaves the squad or the map
		bool  BannerOnSwap   = false;           // ... or swaps to another profession
		bool  BannerOnShare  = true;            // somebody shares an order, or one is taken over
		bool  BannerOnAsk    = false;           // somebody asks the squad for the order

		// --- The turn reaching us. Three signals, each switchable for "up" and for "backup" on its own.
		bool  UpBanner       = false;
		bool  BackupBanner   = false;
		int   UpSound        = 0;               // Notify::Sound
		int   BackupSound    = 0;
		int   SoundVolume    = 60;              // 0-100
		std::string SoundFile;                  // used when a sound is set to "file..."
		bool  UpFlash        = true;            // a coloured pulse along the edges of the screen
		bool  BackupFlash    = true;
		float FlashStrength  = 0.25f;           // peak opacity, 0-1
		float UpFlashColor[3]     = { 0.35f, 0.95f, 0.40f };
		float BackupFlashColor[3] = { 1.00f, 0.62f, 0.15f };

		std::string LastSeenVersion;            // the "all set" alert is for the first run of a new build
		bool  EditorAllProfessions = false;     // editor lists professions without a revive skill too
		int   OverlayMessages = 0;              // passing messages: 0 below the turn window, 1 above it, 2 not shown
		int   BannerStyle   = 1;                // Banner::Style
		// Two groups of banners, each with its own size (times the overlay's base font size) and place (percent of
		// the screen: the middle of the stack across, its top down). Messages: somebody left, an order was shared,
		// a problem. Alerts: your turn, backup, the Illusion of Life countdown.
		float BannerInfoSize  = 1.3f;
		float BannerInfoX     = 50.0f;
		float BannerInfoY     = 20.0f;
		float BannerAlertSize = 2.2f;
		float BannerAlertX    = 50.0f;
		float BannerAlertY    = 30.0f;
		float BannerInfoColor[3]     = { 0.93f, 0.93f, 0.95f };
		float BannerUpColor[3]       = { 0.35f, 0.95f, 0.40f };
		float BannerBackupColor[3]   = { 1.00f, 0.62f, 0.15f };
		float BannerIllusionColor[3] = { 0.90f, 0.50f, 1.00f };
		float BannerSeconds = 4.0f;             // how long a banner stays on screen, fade included
		bool  IllusionCountdown   = true;       // banner counting down before a player revived by Illusion of Life goes down
		int   IllusionWarnSeconds = 5;          // ... starting this many seconds before
		int   IllusionSound = 7;                // Notify::Sound when the countdown starts: the bell
		bool  IllusionFlash = true;
		float IllusionFlashColor[3] = { 0.82f, 0.35f, 0.95f }; // pink-purple: nothing like your turn or backup
		bool  IllusionNumberBelow = true;       // the countdown's figures under the names, nearer the middle of the screen
		// The field recorder writes squad members' account and character names to a CSV. It exists to collect
		// test data, so it is off unless somebody deliberately turns it on.
		bool  RecordFieldLogs = false;
	};

	extern Values Current;

	void Load(const std::filesystem::path& aFile);
	// Marks settings as changed; they are written by Flush at most once per second.
	void MarkDirty();
	void Flush(unsigned aNowMs, bool aForce = false);
}
