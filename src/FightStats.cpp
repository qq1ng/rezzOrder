#include "FightStats.h"

#include <algorithm>

namespace Rezz
{
	std::string FightStat::Summary() const
	{
		std::string text = std::to_string(Revived) + " revived";
		text += " - " + std::to_string(Possible) + " possible";
		text += " - " + std::to_string(Downs) + " downs";
		if (Rallied > 0) { text += ", " + std::to_string(Rallied) + " rallied"; }
		if (Died > 0) { text += ", " + std::to_string(Died) + " died"; }
		return text;
	}

	PlayerStat& FightStats::Get(const std::string& aAccount)
	{
		for (PlayerStat& player : m_Players)
		{
			if (player.Account == aAccount) { return player; }
		}
		PlayerStat& player = m_Players.emplace_back();
		player.Account = aAccount;
		return player;
	}

	void FightStats::Begin(uint64_t aTimeMs)
	{
		if (m_Active) { return; }
		m_Active = true;
		m_StartMs = aTimeMs;
		m_Fight = FightStat{};
		m_Fight.StartMs = aTimeMs;
		m_Open.clear();
		m_Players.clear();
		m_Credits.clear();
		m_LastWaveMs = 0;
		m_LastEventMs = aTimeMs;
		m_WaveLeft = 0;
	}

	void FightStats::OnDown(uint64_t aTimeMs, const std::string& aAccount, const Chance& aChance)
	{
		if (!m_Active) { Begin(aTimeMs); }
		m_LastEventMs = std::max(m_LastEventMs, aTimeMs);
		m_Fight.Downs++;

		// Several players going down at once are one wave: two skills that can pick up three allies each are
		// three chances, not one per body on the floor.
		if (m_LastWaveMs == 0 || aTimeMs > m_LastWaveMs + kWaveMs)
		{
			m_LastWaveMs = aTimeMs;
			m_WaveLeft = aChance.Capacity;
		}
		if (m_WaveLeft > 0)
		{
			m_WaveLeft--;
			m_Fight.Possible++;
		}

		std::erase_if(m_Open, [&](const OpenDown& aDown) { return aDown.Account == aAccount; });
		m_Open.push_back(OpenDown{ aAccount, aTimeMs, aChance.UpAccount, false });
	}

	void FightStats::Resolve(const OpenDown& aDown, uint64_t aTimeMs, bool aDied)
	{
		if (aDown.UpAccount.empty() || aDown.Answered) { return; }
		// Their skill was ready and somebody was on the floor long enough for it to be their call.
		uint64_t lasted = aTimeMs > aDown.TimeMs ? aTimeMs - aDown.TimeMs : 0;
		if (lasted < kMissedTurnMs && !aDied) { return; }
		PlayerStat& player = Get(aDown.UpAccount);
		player.MissedTurns++;
		if (aDied) { player.LetDie++; }
	}

	void FightStats::OnUp(uint64_t aTimeMs, const std::string& aAccount, const std::string& aReviver)
	{
		auto it = std::find_if(m_Open.begin(), m_Open.end(), [&](const OpenDown& aDown) { return aDown.Account == aAccount; });
		if (it == m_Open.end()) { return; } // up without a down we saw: out of range when it happened
		m_LastEventMs = std::max(m_LastEventMs, aTimeMs);
		if (aReviver.empty())
		{
			m_Fight.Rallied++;
			Resolve(*it, aTimeMs, false); // nobody's skill saved them, so a slow turn still counts as missed
		}
		else { m_Fight.Revived++; }
		m_Open.erase(it);
	}

	void FightStats::OnDead(uint64_t aTimeMs, const std::string& aAccount)
	{
		auto it = std::find_if(m_Open.begin(), m_Open.end(), [&](const OpenDown& aDown) { return aDown.Account == aAccount; });
		if (it == m_Open.end()) { return; }
		m_LastEventMs = std::max(m_LastEventMs, aTimeMs);
		m_Fight.Died++;
		Resolve(*it, aTimeMs, true);
		m_Open.erase(it);
	}

	void FightStats::CloseCast(PlayerStat& aPlayer)
	{
		if (aPlayer.RevivedAtCast >= 0 && aPlayer.Revived == aPlayer.RevivedAtCast)
		{
			// Their cast picked nobody up. Somebody else being credited at that moment means they were beaten
			// to the ally; otherwise there was nobody for them to get.
			bool beaten = std::any_of(m_Credits.begin(), m_Credits.end(), [&](const Credit& aCredit)
			{
				uint64_t apart = aCredit.TimeMs > aPlayer.CastAtMs ? aCredit.TimeMs - aPlayer.CastAtMs
					: aPlayer.CastAtMs - aCredit.TimeMs;
				return aCredit.Reviver != aPlayer.Account && apart <= kOverlapMs;
			});
			if (beaten) { aPlayer.Overlapped++; }
			else if (!aPlayer.CastHadDowns) { aPlayer.OnNothing++; }
			else { aPlayer.TooLate++; }
		}
		aPlayer.RevivedAtCast = -1;
	}

	void FightStats::OnCast(uint64_t aTimeMs, const std::string& aAccount, bool aUsed, CastEnd aEnd, bool aInTurn)
	{
		if (!m_Active) { Begin(aTimeMs); }
		m_LastEventMs = std::max(m_LastEventMs, aTimeMs);
		PlayerStat& player = Get(aAccount);
		if (!aUsed)
		{
			switch (aEnd)
			{
				case CastEnd::ByHand:      player.ByHand++; break;
				case CastEnd::Interrupted: player.Interrupted++; break;
				case CastEnd::Movement:    player.Movement++; break;
				case CastEnd::WentDown:    player.WentDown++; break;
				default: break;
			}
			return;
		}

		CloseCast(player); // whatever the previous cast picked up is settled by now
		player.Used++;
		player.RevivedAtCast = player.Revived;
		player.CastAtMs = aTimeMs;
		player.CastHadDowns = !m_Open.empty();
		if (!aInTurn) { player.OutOfTurn++; }

		// How long the oldest ally they could have picked up had been lying there.
		uint64_t oldest = 0;
		for (OpenDown& down : m_Open)
		{
			if (down.UpAccount == aAccount) { down.Answered = true; }
			if (down.TimeMs <= aTimeMs) { oldest = std::max(oldest, aTimeMs - down.TimeMs); }
		}
		if (oldest > 0)
		{
			player.TotalLateMs += oldest;
			player.LateCasts++;
			player.SlowestMs = std::max(player.SlowestMs, oldest);
		}
	}

	void FightStats::OnRevived(uint64_t aTimeMs, const std::string& aReviver)
	{
		if (aReviver.empty()) { return; }
		Get(aReviver).Revived++;
		m_Credits.push_back(Credit{ aTimeMs, aReviver });
	}

	bool FightStats::Close(uint64_t aTimeMs, int aNumber, FightStat& aOut)
	{
		if (!m_Active) { return false; }
		m_Active = false;

		// Anyone still on the floor when the fight ended: counted as a death, since the fight is over either way.
		for (const OpenDown& down : m_Open)
		{
			m_Fight.Died++;
			Resolve(down, aTimeMs, true);
		}
		m_Open.clear();

		for (PlayerStat& player : m_Players) { CloseCast(player); }

		m_Fight.Number = aNumber;
		m_Fight.EndMs = m_LastEventMs > m_Fight.StartMs ? m_LastEventMs : aTimeMs;
		m_Fight.Players = m_Players;
		std::sort(m_Fight.Players.begin(), m_Fight.Players.end(), [](const PlayerStat& a, const PlayerStat& b)
		{
			if (a.Revived != b.Revived) { return a.Revived > b.Revived; }
			if (a.Used != b.Used) { return a.Used > b.Used; }
			return a.Account < b.Account;
		});

		bool worthKeeping = m_Fight.Downs > 0 || !m_Fight.Players.empty();
		aOut = m_Fight;
		m_Fight = FightStat{};
		m_Players.clear();
		return worthKeeping;
	}
}
