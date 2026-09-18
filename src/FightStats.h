#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReviveSkills.h"

// What the squad's revive tools did in one fight, worked out from the same events the tracker runs on.
// Pure logic: the session feeds it and decides when a fight starts and ends.
//
// A "fight" here is an engagement, not an arcdps combat block: arcdps ends a fight at every lull, so the
// session closes one only after the squad has been out of combat for a while (Session::kRotationResetMs).
namespace Rezz
{
	// How a cast ended. arcdps reports the reason, so a cancel by hand reads differently from a stun.
	enum class CastEnd : uint8_t { Full, ByHand, Interrupted, Movement, WentDown };

	struct PlayerStat
	{
		std::string Account;
		int Revived      = 0; // allies their casts picked up
		int Used         = 0; // casts that spent the skill
		int Overlapped   = 0; // ... spent on an ally another revive got to first
		int OnNothing    = 0; // ... spent while nobody was down at all
		int TooLate      = 0; // ... spent with allies down, and none of them got up from it
		int ByHand       = 0; // cancelled by the player
		int Interrupted  = 0; // cancelled by crowd control
		int Movement     = 0; // cancelled by moving or dodging
		int WentDown     = 0; // cancelled by going down mid-cast
		int OutOfTurn    = 0; // spent while it was somebody else's turn
		int MissedTurns  = 0; // it was their turn, somebody was down, and they didn't cast
		int LetDie       = 0; // ... and that down died
		uint64_t SlowestMs = 0; // longest they took from a down to their cast
		uint64_t TotalLateMs = 0;
		int      LateCasts   = 0;
		int      RevivedAtCast = -1; // Revived as it stood at their last spent cast, for judging that cast
		uint64_t CastAtMs      = 0;  // when that cast was
		bool     CastHadDowns  = false; // ... and whether anybody was down at the time

		uint64_t AverageLateMs() const { return LateCasts > 0 ? TotalLateMs / static_cast<uint64_t>(LateCasts) : 0; }
	};

	struct FightStat
	{
		int      Number  = 0; // 1-based within this session
		uint64_t StartMs = 0;
		uint64_t EndMs   = 0;
		int Downs    = 0;
		int Revived  = 0; // got up from a revive skill
		int Rallied  = 0; // got up without one: a kill, or somebody reviving them by hand
		int Died     = 0;
		int Possible = 0; // downs that happened while a revive skill was ready to be used on them
		std::vector<PlayerStat> Players; // most saved first

		uint64_t LengthMs() const { return EndMs > StartMs ? EndMs - StartMs : 0; }
		// "12 revived - 16 possible - 18 downs", the line the messages strip shows.
		std::string Summary() const;
	};

	class FightStats
	{
	public:
		// A down with nobody able to revive it counts differently from one the squad simply missed, so the
		// session passes in what it knows at that moment: whose turn it was (empty if nobody could act) and
		// how many allies the squad's ready skills could still pick up.
		struct Chance
		{
			std::string UpAccount;   // whose turn it was, if their skill was ready
			int         Capacity = 0; // allies the ready skills could revive between them
		};

		// Downs this close together are one wave, so one ready skill can't count as a chance for all of them.
		static constexpr uint64_t kWaveMs = 1000;
		// A turn missed: their skill was ready, somebody was down, and this long passed without a cast. Short,
		// because somebody else stepping in after three seconds still means the turn was not taken.
		static constexpr uint64_t kMissedTurnMs = 3000;
		// A revive credited this close to a cast that got nobody is the one that beat it to the ally.
		static constexpr uint64_t kOverlapMs = 1500;

		bool Active() const { return m_Active; }
		void Begin(uint64_t aTimeMs);
		void OnDown(uint64_t aTimeMs, const std::string& aAccount, const Chance& aChance);
		// aReviver empty: they got up without a revive skill (a kill, or revived by hand).
		void OnUp(uint64_t aTimeMs, const std::string& aAccount, const std::string& aReviver);
		void OnDead(uint64_t aTimeMs, const std::string& aAccount);
		void OnCast(uint64_t aTimeMs, const std::string& aAccount, bool aUsed, CastEnd aEnd, bool aInTurn);
		// A revive skill's cast was credited with an ally getting up.
		void OnRevived(uint64_t aTimeMs, const std::string& aReviver);
		// Closes the fight. Returns false when nothing worth keeping happened in it.
		bool Close(uint64_t aTimeMs, int aNumber, FightStat& aOut);

	private:
		struct OpenDown
		{
			std::string Account;
			uint64_t    TimeMs;
			std::string UpAccount; // whose turn it was when they went down
			bool        Answered = false; // the player whose turn it was has since cast
		};

		PlayerStat& Get(const std::string& aAccount);
		// A down that ended: counts the turn as missed when the player who was up never cast for it.
		void Resolve(const OpenDown& aDown, uint64_t aTimeMs, bool aDied);
		// The cast before this one picked nobody up.
		void CloseCast(PlayerStat& aPlayer);

		bool                    m_Active = false;
		uint64_t                m_StartMs = 0;
		uint64_t                m_LastWaveMs = 0;
		uint64_t                m_LastEventMs = 0; // event time of the last thing that happened in the fight
		int                     m_WaveLeft = 0; // chances left in the wave being counted
		FightStat               m_Fight;
		struct Credit
		{
			uint64_t    TimeMs;
			std::string Reviver;
		};
		std::vector<Credit>     m_Credits; // who was credited with an ally, and when
		std::vector<OpenDown>   m_Open;
		std::vector<PlayerStat> m_Players;
	};
}
