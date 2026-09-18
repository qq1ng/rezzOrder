#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "Arc.h"
#include "FightStats.h"
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
	// ShareRequested: somebody asked to be put in the order. Substituted: somebody moved into the subgroup of a
	// player in the order who was benched, and took their place. FightSummary: a fight ended and its stats are in.
	enum class NoticeKind : uint8_t { LeftSquad, LeftMap, Returned, ChangedProfession, OrderCleared,
		ShareApplied, ShareOffered, ShareRequested, Substituted, FightSummary };

	// Somebody asking to be put in the order ("!rezzorder add 3").
	struct JoinRequest
	{
		std::string Account;   // theirs, with the leading ':'
		int         Place = 0; // the place they asked for, 1-based; 0: no preference, so the end
		uint64_t    TimeMs = 0;
	};

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
		uint16_t                 Bench    = 0;                // bench subgroup it names (Share::kBenchLast: the last), 0 none
	};

	// Somebody revived by Illusion of Life. They go down again when it runs out, unless they rally first.
	struct IllusionTarget
	{
		std::string Account;
		uint64_t    EndsInMs = 0;
		bool        OurCast  = false; // we cast it: we get the countdown even with our own revive spent on it
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
		uint16_t                  Bench = 0;       // the squad's bench subgroup for bench swaps (Share::kBenchLast: the last), 0 none
		bool                      SquadInCombat = false;
		uint64_t                  NowMs         = 0;
		std::string               SelfAccount;
		bool                      HasShare = false;   // a shared order is waiting to be accepted
		SharedOrder               Share;
		// Players who asked to be put in the order, oldest first, and only on the client that answers them.
		std::vector<JoinRequest>  Requests;
		std::vector<IllusionTarget> Illusions;        // other players under Illusion of Life, soonest to run out first
		bool                      SelfCanRevive = false; // our own revive skill is ready and we are on our feet to use it
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
		// A request nobody answered stops asking after this long: by then the squad has moved on.
		static constexpr uint64_t kRequestShowMs = 60 * 1000;
		// At most this many waiting at once, so a squad forming up cannot fill the screen.
		static constexpr size_t kMaxRequests = 6;
		// How soon the same person asking again is announced again, rather than only refreshing the window.
		static constexpr uint64_t kAskAgainMs = 30 * 1000;
		// How long the squad has to be out of combat before the turn goes back to the top of the order, and with
		// it the point where one fight's stats are closed. ArcDPS ends a squad fight at every lull: in the field
		// logs of 2026-09-15 a quarter of the breaks between its fights were under five seconds, so a few seconds
		// of quiet is the same fight carrying on. Ten seconds keeps those together while putting the turn back at
		// the top soon enough to be trusted between pushes (decided with the squad, 2026-09-18).
		static constexpr uint64_t kRotationResetMs = 10 * 1000;
		// How long a place stays open for a substitute, or for its player to come back. A bench swap is two
		// moves in the squad window in either order, but the one coming in may still have to swap character
		// (a loading screen) before they count.
		static constexpr uint64_t kSubstituteWindowMs = 120 * 1000;

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
		// One request has been answered (or ignored); empty clears them all.
		void ClearRequest(const std::string& aAccount = {});
		// Who gets asked to answer "!rezzorder add". Only the client whose order it is, by default: in a squad
		// where everyone took the order from the commander, that is exactly one person. Before there is an
		// order to own, the commander is asked, or the lieutenants when no commander is running the addon.
		enum class AnswerRule : uint8_t { Never = 0, WhenOrderIsOurs = 1, Always = 2 };
		void SetAnswerRule(AnswerRule aRule);
		// Bench swaps (decided with the squad, 2026-09-17). Needs Unofficial Extras, the only source of subgroup
		// moves.
		//   - The bench is set with the order (SetBench) and shared with it: a subgroup number, or "last", which
		//     follows the squad as it fills up. A subgroup without anybody from the order can just as well be a
		//     full fighting group, so that alone never makes it the bench. Moving somebody from the order into the
		//     bench takes them out at once; moving them into any other subgroup is a reshuffle.
		//   - A subgroup with somebody from the order in it is never the bench: some nights there is no bench and
		//     the last subgroup is a fighting group, sometimes with one player from the order alone in it.
		//   - Leaving the squad, or swapping to a character that isn't a sure reviver, takes them out too.
		//   - The place stays open for kSubstituteWindowMs. A sure reviver who moves (not joins) into the subgroup
		//     it was left from takes it; the player who left it gets it back by returning.
		//   - Squads rotate three or more players at once (field log 2026-09-17: off the bench into subgroup 1,
		//     subgroup 1 into 2, subgroup 2 to the bench), so the one coming in rarely lands in the subgroup that
		//     was left. Failing a match by subgroup, a lone sure reviver who came off the bench takes the place.
		//   - A sure reviver is a druid or a troubadour, or anyone seen using a revive skill on this character:
		//     not every warrior carries Battle Standard.
		//   - A player in the order swapping to a sure reviver keeps their place.
		void SetSubstitutes(bool aEnabled);
		// The bench: a subgroup, Share::kBenchLast, or 0 none (no bench swaps; leaving and swapping character
		// still leave a place open).
		void SetBench(uint16_t aBench);
		const std::vector<std::string>& Order() const { return m_Tracker.Order(); }

		SessionView GetView(uint64_t aNowMs) const;
		std::vector<Notice> TakeNotices();

		const Tracker& GetTracker() const { return m_Tracker; }
		// What each fight of this session came to, oldest first. Kept in memory only.
		const std::vector<FightStat>& Fights() const { return m_Fights; }

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
		// Who could act on a down happening now, for the fight stats.
		FightStats::Chance ChanceNow(uint64_t aTimeMs) const;
		// Moves ups that have had their chance to be matched with a revive into the fight stats.
		void SettleUps(uint64_t aNowMs);
		// Drains the tracker's revive credits into the fight stats.
		void TakeAttributions();
		// Whether this client is the one that answers players asking to be put in the order.
		bool AnswersRequests() const;
		bool SelfCanRevive(uint64_t aNowMs) const;
		// A completed Illusion of Life cast (aIsCast) or an ally getting up, matched against the other kind at the
		// same instant: stands in for the effect's apply until one has been seen.
		void MatchIllusion(uint64_t aTimeMs, const std::string& aAccount, bool aIsCast);
		// Takes somebody out of the order and the precast list, leaving everyone else's turn alone. With a
		// subgroup (and substitutes on) their place stays open for a substitute moving into that subgroup.
		void DropFromOrder(const std::string& aAccount, uint16_t aOpenFor = 0, uint64_t aNowMs = 0);
		// Pairs open places with sure revivers who moved into the subgroup they were left from.
		void MatchSubstitutes(uint64_t aNowMs);
		// Puts aAccount into the open place m_Vacancies[aIndex] and closes it.
		void FillPlace(size_t aIndex, const std::string& aAccount);
		// The player who left a place gets it back: rejoining the squad (aSubgroup 0), or returning to the
		// subgroup they left it from.
		bool ReclaimPlace(const std::string& aAccount, uint64_t aNowMs, uint16_t aSubgroup);
		bool SureReviver(const RosterMember& aMember) const;
		// Whether moving aMover into aGroup puts them on the bench. Called before the move is recorded.
		bool IsBench(uint16_t aGroup, const std::string& aMover) const;
		// Unofficial Extras' subgroup for a player, 0 unknown.
		uint16_t SubgroupOf(const std::string& aAccount) const;

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
		uint64_t                                      m_OutOfCombatSinceMs = 0; // 0: in combat, or not known yet
		struct IllusionState
		{
			uint64_t    EndsAt = 0; // event time it runs out
			std::string Caster;
		};
		struct RecentEvent
		{
			uint64_t    TimeMs;
			std::string Account;
		};
		std::unordered_map<std::string, IllusionState> m_Illusions; // who is under Illusion of Life
		// The first Illusion of Life of a session arrives without its effect's apply: every session in the field
		// logs of 2026-09-15 to 2026-09-17, in a squad and in a party. Until an apply has come through, a
		// completed cast and an ally getting up at that very instant stand in for it.
		bool                                          m_IllusionApplySeen = false;
		std::deque<RecentEvent>                       m_RecentIllusionCasts; // who cast, when it completed
		std::deque<RecentEvent>                       m_RecentGotUp;         // who got up, when
		SharedOrder                                   m_Share;
		bool                                          m_HasShare        = false;
		std::deque<JoinRequest>                       m_Requests;
		// When each player was last announced, so one person typing it over and over cannot fill the window
		// with notices. Their request still refreshes, it just stops being announced again.
		std::unordered_map<std::string, uint64_t>     m_AskedNoticeMs;
		// A place in the order left open: its player went to the bench, left the squad or swapped character.
		struct Vacancy
		{
			std::string Account;          // who left it
			uint16_t    Subgroup   = 0;   // the subgroup they left it from
			uint32_t    Profession = 0;
			uint64_t    TimeMs     = 0;
			size_t      Index      = 0;   // it goes in front of the player now at this place in the order...
			uint32_t    Tie        = 0;   // ... behind the open places there with a lower Tie
			bool        Precast    = false;
		};
		// A player outside the order who moved into a subgroup.
		struct Arrival
		{
			std::string Account;
			uint16_t    Subgroup  = 0;
			uint64_t    TimeMs    = 0;
			bool        FromBench = false; // came off the bench, so they are somebody's substitute wherever they land
		};
		std::deque<Vacancy>                           m_Vacancies;
		std::deque<Arrival>                           m_Arrivals;
		// An ally getting up is only known to be a revive once the cast it belongs to has been matched, which can
		// be a moment later, so ups wait here to be counted as revived or rallied.
		struct PendingUp
		{
			std::string Account;
			uint64_t    TimeMs;    // event time
			uint64_t    ArrivedMs; // when we heard about it
		};
		static constexpr uint64_t kUpSettleMs = 1500;
		static constexpr size_t   kKeepFights = 30;
		FightStats                                    m_Stats;
		std::vector<FightStat>                        m_Fights;
		std::vector<PendingUp>                        m_PendingUps;
		std::vector<Attribution>                      m_Attributions; // matched revives waiting for their up
		int                                           m_FightNumber = 0;
		// Last subgroup Unofficial Extras reported. Kept apart from RosterMember::Subgroup, which arcdps also
		// writes, and never 0: Extras reports 0 for a moment while a player loads into a map.
		std::unordered_map<std::string, uint16_t>     m_Subgroups;
		uint16_t                                      m_BenchGroup  = 0; // the squad's bench setting, 0 none
		uint16_t                                      m_LastBenched = 0; // subgroup somebody was last benched into
		bool                                          m_Substitutes = true;
		std::string                                   m_OrderFrom;   // empty: we built this order ourselves
		std::vector<std::string>                      m_Precast;
		AnswerRule                                    m_AnswerRule   = AnswerRule::WhenOrderIsOurs;
		bool                                          m_ShareFromLeaders = true;
		bool                                          m_ShareFromAnyone  = false;
	};
}
