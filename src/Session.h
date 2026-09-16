#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "Arc.h"
#include "Tracker.h"

// Live revive order session: turns arcdps squad events and the Unofficial Extras roster into tracker calls,
// keeps the roster the order editor picks from, and raises notices when a player in the order leaves or
// swaps. Pure logic and not thread-safe; the addon wraps it in a mutex.
//
// Roster behaviour seen in the field log (2026-09-15):
//   - Leaving the squad: Unofficial Extras reports role None at once, arcdps' squad leave follows ~2.7 s later.
//   - Leaving the map (character swap, map hop, disconnect): only an arcdps leave; the player rejoins later.
//   - Our own map change: arcdps removes every squad member within ~1 s before our own self leave, then adds
//     them again after loading. Those leaves must not raise notices.
namespace Rezz
{
	// Roles as numbered by Unofficial Extras (UserRole).
	enum class SquadRole : uint8_t { Leader = 0, Lieutenant = 1, Member = 2, Invited = 3, Applied = 4, None = 5, Unknown = 6 };

	struct RosterMember
	{
		std::string Account;
		std::string Character;         // may be a rank name ("Diamond Legend") in Edge of the Mists
		uint32_t    Profession  = 0;   // arcdps/API profession id, 0 unknown
		uint32_t    Elite       = 0;
		uint16_t    Subgroup    = 0;
		SquadRole   Role        = SquadRole::Unknown;
		bool        OnMap       = false; // announced by arcdps and not removed since
		bool        InSquad     = true;  // false once Unofficial Extras reported them leaving
		bool        IsSelf      = false;
		uint32_t    SeenGroups  = 0;     // bit per ReviveGroup: cast seen, or signet passive present
		uint64_t    LastEventMs = 0;     // last squad event of theirs: silence in a fight means out of range
	};

	// OrderCleared: we left the squad, so the order (which belongs to that squad) was removed.
	enum class NoticeKind : uint8_t { LeftSquad, LeftMap, Returned, ChangedProfession, OrderCleared };

	struct Notice
	{
		NoticeKind  Kind;
		std::string Account;
		std::string Text;
	};

	struct SessionView
	{
		TurnView                  Turn;
		int                       BackupIndex = -1; // row index into Turn.Rows, -1 none (next ready after who is up)
		std::vector<RosterMember> Roster;           // sorted: revive professions first, then by account
		std::vector<std::string>  Order;
		bool                      SquadInCombat = false;
		uint64_t                  NowMs         = 0;
		std::string               SelfAccount;
	};

	// True for professions that have an instant revive utility: guardian, warrior, ranger, elementalist,
	// mesmer, necromancer.
	bool IsReviveProfession(uint32_t aProfession);
	const char* ProfessionShortName(uint32_t aProfession);
	// Whether a character name can be shown. In Edge of the Mists players from other worlds have no real
	// character name: arcdps reports a placeholder ("ag1458"), or the game shows their WvW rank
	// ("Diamond Legend"), which repeats between players.
	bool IsUsableCharacterName(const std::string& aName);
	// ":Name.1234" -> "Name"
	std::string DisplayAccount(const std::string& aAccount);

	class Session
	{
	public:
		// A player who left the map is only reported if they haven't come back after this long, and no mass
		// leave from our own map change happened around it.
		static constexpr uint64_t kLeaveGraceMs = 8 * 1000;
		// Fallback without Unofficial Extras: after our own map change, squad members who are still not back
		// by then are reported as gone. arcdps can take almost a minute to announce everyone again.
		static constexpr uint64_t kResyncAfterMs = 90 * 1000;
		// No event from a player during a squad fight for this long: they are out of arcdps' range.
		static constexpr uint64_t kOutOfRangeMs = 15 * 1000;

		// Squad channel combat event. aNowMs is the arrival time (timeGetTime), used only for roster timing;
		// the tracker works in event time.
		void OnCombat(const ArcDps::EvCombatData& aData, uint64_t aNowMs);
		void OnAgentUpdate(const ArcDps::EvAgentUpdate& aUpdate, uint64_t aNowMs);
		void OnRole(const std::string& aAccount, SquadRole aRole, uint16_t aSubgroup, uint64_t aNowMs);
		// Raises delayed notices. Call regularly (render thread).
		void Tick(uint64_t aNowMs);

		void SetOrder(std::vector<std::string> aAccounts);
		const std::vector<std::string>& Order() const { return m_Tracker.Order(); }

		SessionView GetView(uint64_t aNowMs) const;
		std::vector<Notice> TakeNotices();

		const Tracker& GetTracker() const { return m_Tracker; }

	private:
		struct AgentRef
		{
			std::string Account;
			uint16_t    InstanceId = 0;
		};

		struct PendingLeave
		{
			std::string Account;
			uint64_t    TimeMs;
			uint32_t    Profession;
		};

		// Account of a squad player from an event's agent id, falling back to the map instance id when the
		// agent id changed (arcdps' id change isn't sent to the live feed). Empty when not a squad player.
		std::string ResolveAccount(uint64_t aId, uint16_t aInstanceId);
		std::string AccountByInstance(uint16_t aInstanceId) const;
		RosterMember& GetMember(const std::string& aAccount);
		bool InOrder(const std::string& aAccount) const;
		void Notify(NoticeKind aKind, const std::string& aAccount, std::string aText);
		void OnSelfLeftSquad();

		Tracker                                       m_Tracker;
		std::unordered_map<std::string, RosterMember> m_Roster;
		std::unordered_map<uint64_t, AgentRef>        m_Agents;  // arcdps agent id -> account
		std::unordered_map<uint64_t, uint64_t>        m_Aliases; // unknown agent id -> known agent id
		std::deque<PendingLeave>                      m_PendingLeaves;
		std::unordered_map<std::string, bool>         m_Reported; // account -> a "left" notice was raised
		std::vector<Notice>                           m_Notices;
		std::string                                   m_SelfAccount;
		uint64_t                                      m_LastSelfLeaveMs = 0;
		uint64_t                                      m_ResyncAtMs      = 0;
		bool                                          m_HasRoles        = false; // Unofficial Extras is reporting
		uint64_t                                      m_CombatSinceMs   = 0; // 0: squad not in combat
	};
}
