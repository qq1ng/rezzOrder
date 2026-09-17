#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "ReviveSkills.h"

// Revive order tracker: each player's revive skill state and whose turn it is.
// Pure logic without game, Nexus or UI dependencies. Events come in already resolved to account names
// (the only stable player identity). Times are arcdps event times (timeGetTime ms): cooldowns are
// measured from when a cast really happened, not from when the ~2.65 s delayed notification arrived.
//
// Turn rules (decided with the squad lead, see notes/phase1-tracker.md):
//   - The turn passes when a revive skill is spent, whether or not it revived anyone.
//   - Only the player whose turn it was moves the rotation on. Somebody casting out of turn (including a
//     precast player firing early) spends their own skill and leaves everyone else's place alone.
//   - Players whose skill is on cooldown, or who are downed or dead, are skipped.
//   - During a fight the turn keeps moving down the list: a player whose skill comes back waits until the list
//     comes round to them again. Out of a fight it starts again from the top (the session calls ResetRotation).
namespace Rezz
{
	enum class LifeState : uint8_t { Alive, Downed, Dead };
	enum class SkillState : uint8_t { Ready, Casting, Cooldown };

	struct SkillStatus
	{
		ReviveGroup Group        = ReviveGroup::Count;
		SkillState  State        = SkillState::Ready;
		uint64_t    CastStartMs  = 0;
		uint64_t    LastUsedMs   = 0;
		uint64_t    ReadyAtMs    = 0; // estimate: last use + base WvW recharge (alacrity/chilled not modelled yet)
		uint32_t    Uses         = 0;
		uint32_t    Cancels      = 0;
		uint32_t    Revived      = 0; // allies attributed to this player's casts, all uses
		uint32_t    LastRevived  = 0; // allies attributed to the most recent use
		uint32_t    EarlyRecasts = 0; // cast started while the cooldown estimate still ran (estimate too long)
		bool        Confirmed    = false; // we have seen this skill cast, or its signet passive
	};

	struct PlayerStatus
	{
		std::string              Account;
		LifeState                Life        = LifeState::Alive;
		uint64_t                 LifeSinceMs = 0;
		bool                     Away        = false; // not on our map (left the map or the squad)
		uint64_t                 SeenSinceMs = 0;     // when we started watching them (join, or coming back)
		std::vector<SkillStatus> Skills; // one per revive group this player has been seen using
	};

	enum class Eligibility : uint8_t { Ready, Casting, Cooldown, Downed, Dead, Away };

	struct OrderRow
	{
		std::string         Account;
		const PlayerStatus* Player    = nullptr; // null until the player has produced an event
		Eligibility         Status    = Eligibility::Ready;
		uint64_t            ReadyInMs = 0;       // for Cooldown
		uint64_t            RechargeMs = 0;     // full recharge of the skill being waited on, for a progress bar
		bool                IsUp      = false;
		bool                IsLastUser = false;
		// Ready is a guess: no cast of theirs has been seen yet and they haven't been watched for a full
		// cooldown, so they may have used their skill before we could see them.
		bool                Unconfirmed = false;
	};

	struct TurnView
	{
		std::vector<OrderRow> Rows;
		int UpIndex        = -1; // -1: nobody in the order can revive right now
		int BackupIndex    = -1; // the next eligible player after the one who is up
		int NextReadyIndex = -1; // when nobody is up: the first player to come off cooldown
	};

	class Tracker
	{
	public:
		// Downed state older than this is treated as unknown (guards against a missed "got up" event).
		static constexpr uint64_t kStaleDownedMs = 120 * 1000;
		// A cast starting this much before the estimated ready time counts as an early recast.
		static constexpr uint64_t kRecastSlackMs = 5 * 1000;
		// Watching a player this long without seeing a cast means their skill really is ready: it is the
		// longest revive cooldown in WvW (Battle Standard, Spirit of Nature).
		static constexpr uint64_t kConfirmAfterMs = 120 * 1000;

		// A different order restarts the rotation at the first player; the same order keeps it.
		void SetOrder(std::vector<std::string> aAccounts);
		// Takes one player out without restarting the rotation: somebody leaving is not a new order, and the
		// people still in it keep their turn. Returns whether they were in the order at all.
		bool RemoveFromOrder(const std::string& aAccount);
		// Puts a player in at a place (clamped to the end) without restarting the rotation.
		void InsertIntoOrder(size_t aIndex, const std::string& aAccount);
		// Forgets who cast last, so the turn starts again from the first player who can revive.
		void ResetRotation() { m_LastOrderedUser.clear(); }
		const std::vector<std::string>& Order() const { return m_Order; }

		void OnCastStart(uint64_t aTimeMs, const std::string& aAccount, uint32_t aSkillId);
		void OnCastStop(uint64_t aTimeMs, const std::string& aAccount, uint32_t aSkillId, uint8_t aStopReason, int32_t aBaseMs);
		// A revive effect performed by something the player owns (the ranger spirit's slams). Only used to
		// attribute allies getting up; the player's skill state changes on their own cast.
		void OnOwnedEffect(uint64_t aTimeMs, const std::string& aOwnerAccount, uint32_t aSkillId);
		void OnLifeState(uint64_t aTimeMs, const std::string& aAccount, LifeState aState);
		// The player started any animation: they can't be dead (a missed "got up" event).
		void OnActivity(uint64_t aTimeMs, const std::string& aAccount);
		// Player left or came back to our map. Coming back resets them to alive; what happened while they
		// were away wasn't visible.
		void SetAway(uint64_t aTimeMs, const std::string& aAccount, bool aAway);
		// Different skill bar (profession swap): drop the player's skill state.
		void ForgetSkills(const std::string& aAccount);
		// We are watching this player from now on (squad join, or back on our map).
		void MarkSeenSince(uint64_t aTimeMs, const std::string& aAccount);
		// The skill's effect was seen, so it was spent whatever the cast events said. In the field log of
		// 2026-09-17 arcdps reported an Illusion of Life as cancelled after 37 ms and the revive landed a second
		// later: without this the caster keeps their turn while their skill is recharging.
		void MarkUsed(uint64_t aTimeMs, const std::string& aAccount, ReviveGroup aGroup);
		// A signet passive tells us the signet is off cooldown right now.
		void MarkSkillReady(uint64_t aTimeMs, const std::string& aAccount, ReviveGroup aGroup);

		const PlayerStatus* FindPlayer(const std::string& aAccount) const;
		TurnView GetTurn(uint64_t aNowMs) const;

		// Account of the last player in the order who spent a revive skill, or empty.
		const std::string& LastOrderedUser() const { return m_LastOrderedUser; }

	private:
		// A spent cast or effect waiting to be matched with allies getting up around it.
		struct RecentRevive
		{
			uint64_t    TimeMs;
			std::string Account;
			uint32_t    SkillId;
			ReviveGroup Group;
			uint8_t     Attributed = 0;
		};

		// An ally who got up from downed, waiting to be matched with a revive cast or effect.
		struct RecentUp
		{
			uint64_t TimeMs;
			bool     Attributed = false;
		};

		PlayerStatus& GetPlayer(const std::string& aAccount);
		SkillStatus& GetSkill(PlayerStatus& aPlayer, ReviveGroup aGroup);
		void AddRevive(uint64_t aTimeMs, const std::string& aAccount, const ReviveSkill& aSkill);
		void Attribute(RecentRevive& aRevive);
		void Prune(uint64_t aTimeMs);

		std::vector<std::string>                      m_Order;
		std::unordered_map<std::string, PlayerStatus> m_Players;
		std::string                                   m_LastOrderedUser;
		std::deque<RecentRevive>                      m_RecentRevives;
		std::deque<RecentUp>                          m_RecentUps;
	};
}
