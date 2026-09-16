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
	// ShareApplied: someone's shared order was taken over. ShareOffered: it is waiting for us to accept it.
	// ShareRequested: somebody asked the squad for the order.
	enum class NoticeKind : uint8_t { LeftSquad, LeftMap, Returned, ChangedProfession, OrderCleared,
		ShareApplied, ShareOffered, ShareRequested };

	// A revive order somebody sent in squad chat.
	struct SharedOrder
	{
		std::string              From;                        // account of the sender, with the leading ':'
		SquadRole                FromRole = SquadRole::Unknown;
		std::vector<std::string> Accounts;                    // resolved against our own squad roster
		std::vector<std::string> Precast;                     // of those, the ones marked free to fire early
		std::vector<std::string> Unknown;                     // names in the message we could not place
		int                      OurPlaceNow  = 0;            // 1-based place in the order we have, 0 not in it
		int                      OurPlaceThen = 0;            // ... and in the one being offered
		uint64_t                 TimeMs   = 0;
	};

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
		std::vector<std::string>  Precast;         // of those, the ones free to cast before their turn
		bool                      SquadInCombat = false;
		uint64_t                  NowMs         = 0;
		std::string               SelfAccount;
		bool                      HasShare = false;   // a shared order is waiting to be accepted
		SharedOrder               Share;
		bool                      HasRequest = false; // somebody asked us for the order
		std::string               RequestFrom;        // their account, with the leading ':'
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
		// A request for the order that nobody answered stops asking after this long.
		static constexpr uint64_t kRequestShowMs = 60 * 1000;
		// How soon the same person asking again is announced again, rather than only refreshing the window.
		static constexpr uint64_t kAskAgainMs = 30 * 1000;

		// Squad channel combat event. aNowMs is the arrival time (timeGetTime), used only for roster timing;
		// the tracker works in event time.
		void OnCombat(const ArcDps::EvCombatData& aData, uint64_t aNowMs);
		void OnAgentUpdate(const ArcDps::EvAgentUpdate& aUpdate, uint64_t aNowMs);
		void OnRole(const std::string& aAccount, SquadRole aRole, uint16_t aSubgroup, uint64_t aNowMs);
		// Raises delayed notices. Call regularly (render thread).
		void Tick(uint64_t aNowMs);

		// The order as this client set it: ours to answer questions about.
		void SetOrder(std::vector<std::string> aAccounts);
		// Who in it may fire early. Kept beside the order, not inside the rotation.
		void SetPrecast(std::vector<std::string> aAccounts);
		const std::vector<std::string>& Precast() const { return m_Precast; }
		// Whether the order on screen was built here, rather than taken over from somebody else's share.
		bool OrderIsOurs() const { return m_OrderFrom.empty(); }
		// Our own place in the order, 1-based; 0 when we are not in it.
		int SelfPlace() const;
		// A squad chat message as Unofficial Extras reports it (account name with the leading ':').
		void OnChatMessage(const std::string& aAccount, const std::string& aText, uint64_t aNowMs);
		// Who may set the order for us without being asked. Leaders and lieutenants by default.
		void SetShareRules(bool aFromLeaders, bool aFromAnyone);
		// Takes over the order that is waiting, or throws it away.
		void AcceptShare();
		void DismissShare();
		// The request for the order has been answered (or ignored).
		void ClearRequest();
		// Who gets asked to answer "!rezz?". Only the client whose order it is, by default: in a squad where
		// everyone took the order from the commander, that is exactly one person.
		enum class AnswerRule : uint8_t { Never = 0, WhenOrderIsOurs = 1, Always = 2 };
		void SetAnswerRule(AnswerRule aRule);
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
			int         OurStanding; // where the turn stood before they dropped out
		};

		// Account of a squad player from an event's agent id, falling back to the map instance id when the
		// agent id changed (arcdps' id change isn't sent to the live feed). Empty when not a squad player.
		std::string ResolveAccount(uint64_t aId, uint16_t aInstanceId);
		std::string AccountByInstance(uint16_t aInstanceId) const;
		RosterMember& GetMember(const std::string& aAccount);
		bool InOrder(const std::string& aAccount) const;
		void Notify(NoticeKind aKind, const std::string& aAccount, std::string aText);
		// "3. Gorath" for a player in the order, else just their name.
		std::string Named(const std::string& aAccount) const;
		// " - you are now 2. (was 4.)" when somebody else's order moves us, else empty.
		std::string PlaceChange(int aPlaceBefore) const;
		// Whose turn it is right now as far as we are concerned: 1 we are up, 2 we are the backup, 0 neither.
		int SelfStanding(uint64_t aNowMs) const;
		// " - you are up now" when somebody dropping out of the rotation moved the turn to us.
		std::string StandingChange(int aBefore, int aAfter) const;
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
		SharedOrder                                   m_Share;
		bool                                          m_HasShare        = false;
		std::string                                   m_RequestFrom;
		uint64_t                                      m_RequestAtMs     = 0;
		// Who we last told the player about, so one person typing "?rezzorder" over and over cannot fill the
		// window with notices. Their question still refreshes, it just stops being announced again.
		std::string                                   m_AskedNoticeFrom;
		uint64_t                                      m_AskedNoticeMs   = 0;
		std::string                                   m_OrderFrom;   // empty: we built this order ourselves
		std::vector<std::string>                      m_Precast;
		AnswerRule                                    m_AnswerRule   = AnswerRule::WhenOrderIsOurs;
		bool                                          m_ShareFromLeaders = true;
		bool                                          m_ShareFromAnyone  = false;
	};
}
