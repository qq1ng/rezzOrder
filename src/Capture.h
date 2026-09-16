#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <Windows.h>

#include "Arc.h"

struct UserInfo;
struct SquadMessageInfo;

// Phase 0 field recorder: writes the events needed to validate revive tracking to CSV, unattended.
// On WvW maps it records revive casts, downed/up/dead, revive-related buffs, cast/recharge speed
// conditions on revive casters, squad roster/commands, plus periodic position, visibility and delay rows.
// Handlers may be called from any thread; Tick() only from the render thread.
namespace Capture
{
	enum Channel { CH_SQUAD, CH_LOCAL, CH_COUNT };
	enum Who     { WHO_SELF, WHO_OTHER, WHO_COUNT };
	enum Scope   { SCOPE_ALL, SCOPE_ANIMATION, SCOPE_COUNT };

	struct DelayStat
	{
		uint64_t Count = 0;
		uint64_t Sum   = 0;
		uint32_t Last  = 0;
		uint32_t Min   = UINT32_MAX;
		uint32_t Max   = 0;

		void Add(uint32_t aMs);
	};

	// One line in the window's cast lists: an animation start/stop, a downed/up/dead change or a mark.
	struct FeedRow
	{
		uint32_t    ArriveMs = 0;
		int64_t     DelayMs  = -1;
		const char* Channel  = "";
		std::string Who;
		bool        IsSelf   = false;
		uint32_t    SkillId  = 0;
		std::string Skill;
		std::string Event;
		std::string Verdict;
		bool        IsMark   = false;
	};

	struct Member
	{
		std::string Account;
		uint8_t     Role     = 0;
		uint8_t     Subgroup = 0;
	};

	struct ChatCommand
	{
		uint32_t    ArriveMs = 0;
		std::string Account;
		std::string Role;
		std::string Text;
		bool        WouldAccept = false;
	};

	struct Snapshot
	{
		DelayStat                Delay[CH_COUNT][WHO_COUNT][SCOPE_COUNT];
		std::deque<FeedRow>      ReviveFeed; // revive skills + downed/up/dead, anyone in squad
		std::deque<FeedRow>      SelfFeed;   // every animation of the local player
		std::vector<Member>      Squad;
		std::deque<ChatCommand>  Commands;
		std::string              SelfAccount;
		std::string              SelfCharacter;
		std::string              LogPath;
		uint64_t                 Rows      = 0;
		uint32_t                 MarkCount = 0;
		uint32_t                 NowMs     = 0;
	};

	struct Status
	{
		uint64_t    ArcEvents = 0; // arcdps events received (any map)
		uint64_t    UeEvents  = 0; // Unofficial Extras squad/chat callbacks received
		uint64_t    Rows      = 0;
		uint64_t    Bytes     = 0;
		bool        InWvw     = false;
		std::string LogPath;
	};

	struct TickInfo
	{
		uint32_t NowMs = 0;
		bool     HasPosition = false;
		float    Position[3] = {}; // MumbleLink avatar position, meters
	};

	extern std::atomic<bool> LogAllEvents; // write every arcdps event on any map (large files)
	extern std::atomic<bool> LogKeys;      // write key presses (skipped while a text box is focused)

	bool Start(const std::filesystem::path& aLogDirectory, const char* aAddonVersion);
	void Stop();
	Snapshot GetSnapshot();
	Status GetStatus();

	void OnCombat(Channel aChannel, const ArcDps::EvCombatData* aData);
	void OnAgentUpdate(const char* aEventName, const ArcDps::EvAgentUpdate* aUpdate);
	void OnSquadUpdate(const UserInfo* aUsers, uint64_t aCount);
	void OnChatMessage(const SquadMessageInfo* aMessage);
	void OnKey(UINT aMsg, WPARAM aWParam, LPARAM aLParam, bool aTextboxFocused);
	void OnMapChange(uint32_t aMapId, uint8_t aMapType, bool aIsCompetitive);
	void Mark(const std::string& aLabel);
	void LogInfo(const std::string& aDetail);
	void Tick(const TickInfo& aTick);

	const char* RoleName(uint8_t aRole);
}
