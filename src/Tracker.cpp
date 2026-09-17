#include "Tracker.h"

#include <algorithm>
#include <cstdlib>

namespace Rezz
{
	namespace
	{
		constexpr uint64_t kRecentWindowMs = 10 * 1000;
		constexpr uint64_t kStuckCastMs    = 10 * 1000; // a cast without a stop event is forgotten after this

		// When an ally gets up relative to the recorded stop of the cast/effect that revived them (field data,
		// notes/phase0-results.md sections 16 and 19): Illusion of Life at the stop, Battle Standard ~70 ms and
		// spirit slams ~115 ms before it. Skills without field data get a symmetric window.
		struct Window { int64_t From; int64_t Expected; int64_t To; };

		Window ReviveWindow(uint32_t aSkillId)
		{
			switch (aSkillId)
			{
				case 10244: return { -150, 0, 450 };    // Illusion of Life
				case 14419: return { -450, -70, 300 };  // Battle Standard
				case 12601:
				case 69336: return { -450, -115, 150 }; // Nature's Renewal slams
				default:    return { -450, 0, 450 };
			}
		}
	}

	void Tracker::SetOrder(std::vector<std::string> aAccounts)
	{
		// A changed order starts again from the top; setting the same order again keeps the rotation.
		if (aAccounts != m_Order) { m_LastOrderedUser.clear(); }
		m_Order = std::move(aAccounts);
	}

	bool Tracker::RemoveFromOrder(const std::string& aAccount)
	{
		auto it = std::find(m_Order.begin(), m_Order.end(), aAccount);
		if (it == m_Order.end()) { return false; }

		// GetTurn scans from whoever cast last, so losing that player would send the rotation back to the top.
		// Handing the marker to the player in front of them leaves the scan starting where it already was.
		if (aAccount == m_LastOrderedUser)
		{
			m_LastOrderedUser = it == m_Order.begin() ? std::string() : *(it - 1);
		}
		m_Order.erase(it);
		return true;
	}

	void Tracker::InsertIntoOrder(size_t aIndex, const std::string& aAccount)
	{
		if (std::find(m_Order.begin(), m_Order.end(), aAccount) != m_Order.end()) { return; }
		m_Order.insert(m_Order.begin() + static_cast<std::ptrdiff_t>(std::min(aIndex, m_Order.size())), aAccount);
	}

	PlayerStatus& Tracker::GetPlayer(const std::string& aAccount)
	{
		PlayerStatus& player = m_Players[aAccount];
		if (player.Account.empty()) { player.Account = aAccount; }
		return player;
	}

	SkillStatus& Tracker::GetSkill(PlayerStatus& aPlayer, ReviveGroup aGroup)
	{
		for (SkillStatus& skill : aPlayer.Skills)
		{
			if (skill.Group == aGroup) { return skill; }
		}
		SkillStatus& skill = aPlayer.Skills.emplace_back();
		skill.Group = aGroup;
		return skill;
	}

	const PlayerStatus* Tracker::FindPlayer(const std::string& aAccount) const
	{
		auto it = m_Players.find(aAccount);
		return it == m_Players.end() ? nullptr : &it->second;
	}

	void Tracker::OnCastStart(uint64_t aTimeMs, const std::string& aAccount, uint32_t aSkillId)
	{
		const ReviveSkill* info = FindReviveSkill(aSkillId);
		if (info == nullptr || !info->IsPlayerCast || aAccount.empty()) { return; }

		PlayerStatus& player = GetPlayer(aAccount);
		if (player.Life != LifeState::Alive) { player.Life = LifeState::Alive; player.LifeSinceMs = aTimeMs; } // utilities can't be cast downed
		SkillStatus& skill = GetSkill(player, info->Group);
		if (skill.State == SkillState::Cooldown && aTimeMs + kRecastSlackMs < skill.ReadyAtMs)
		{
			// The game allowed the cast, so the skill was ready: our estimate (or the last "used") was wrong.
			skill.EarlyRecasts++;
		}
		skill.State = SkillState::Casting;
		skill.CastStartMs = aTimeMs;
		skill.Confirmed = true;
	}

	void Tracker::OnCastStop(uint64_t aTimeMs, const std::string& aAccount, uint32_t aSkillId, uint8_t aStopReason, int32_t aBaseMs)
	{
		const ReviveSkill* info = FindReviveSkill(aSkillId);
		if (info == nullptr || !info->IsPlayerCast || aAccount.empty()) { return; }

		// Whose turn it was at the moment of the cast, worked out before the skill is marked spent (which
		// would take the caster out of the running).
		bool wasUp = false;
		if (std::find(m_Order.begin(), m_Order.end(), aAccount) != m_Order.end())
		{
			TurnView before = GetTurn(aTimeMs);
			wasUp = before.UpIndex >= 0 && before.Rows[before.UpIndex].Account == aAccount;
		}

		SkillStatus& skill = GetSkill(GetPlayer(aAccount), info->Group);
		if (!CountsAsUsed(*info, aStopReason, aBaseMs))
		{
			skill.Cancels++;
			skill.State = skill.ReadyAtMs > aTimeMs ? SkillState::Cooldown : SkillState::Ready;
			return;
		}

		skill.Uses++;
		skill.Confirmed = true;
		skill.LastUsedMs = aTimeMs;
		skill.ReadyAtMs = aTimeMs + GetGroupInfo(info->Group).WvwRechargeS * 1000ull;
		skill.State = SkillState::Cooldown;
		skill.LastRevived = 0;

		// Only the player whose turn it was moves the rotation on. Somebody casting out of turn spends their
		// own skill and nothing else: the people ahead of them keep their place, which is the whole point of
		// having agreed an order.
		if (wasUp) { m_LastOrderedUser = aAccount; }

		// The spirit's slams do the reviving; its cast only starts the cooldown.
		if (info->Group != ReviveGroup::SpiritOfNature) { AddRevive(aTimeMs, aAccount, *info); }
	}

	void Tracker::MarkUsed(uint64_t aTimeMs, const std::string& aAccount, ReviveGroup aGroup)
	{
		if (aAccount.empty() || aGroup == ReviveGroup::Count) { return; }
		// The effect of a cast that was already counted, arriving a moment later: nothing new to record.
		constexpr uint64_t kAlreadyCountedMs = 5 * 1000;
		PlayerStatus& player = GetPlayer(aAccount);
		SkillStatus& skill = GetSkill(player, aGroup);
		if (skill.LastUsedMs != 0 && aTimeMs < skill.LastUsedMs + kAlreadyCountedMs) { return; }

		bool wasUp = false;
		if (std::find(m_Order.begin(), m_Order.end(), aAccount) != m_Order.end())
		{
			TurnView before = GetTurn(aTimeMs);
			wasUp = before.UpIndex >= 0 && before.Rows[before.UpIndex].Account == aAccount;
		}
		if (skill.State == SkillState::Casting) { skill.Cancels = skill.Cancels > 0 ? skill.Cancels - 1 : 0; }
		skill.Uses++;
		skill.Confirmed = true;
		skill.LastUsedMs = aTimeMs;
		skill.ReadyAtMs = aTimeMs + GetGroupInfo(aGroup).WvwRechargeS * 1000ull;
		skill.State = SkillState::Cooldown;
		skill.LastRevived = 0;
		if (wasUp) { m_LastOrderedUser = aAccount; }
	}

	void Tracker::OnOwnedEffect(uint64_t aTimeMs, const std::string& aOwnerAccount, uint32_t aSkillId)
	{
		const ReviveSkill* info = FindReviveSkill(aSkillId);
		if (info == nullptr || info->IsPlayerCast || aOwnerAccount.empty()) { return; }
		AddRevive(aTimeMs, aOwnerAccount, *info);
	}

	void Tracker::OnLifeState(uint64_t aTimeMs, const std::string& aAccount, LifeState aState)
	{
		if (aAccount.empty()) { return; }
		PlayerStatus& player = GetPlayer(aAccount);
		bool revived = player.Life == LifeState::Downed && aState == LifeState::Alive;
		player.Life = aState;
		player.LifeSinceMs = aTimeMs;
		if (!revived) { return; }

		Prune(aTimeMs);
		RecentUp& up = m_RecentUps.emplace_back(RecentUp{ aTimeMs });

		// Best matching revive that has already been reported (slam stops arrive after the ally is up; those
		// are matched from AddRevive instead).
		RecentRevive* best = nullptr;
		int64_t bestDistance = 0;
		for (RecentRevive& revive : m_RecentRevives)
		{
			const ReviveSkill* info = FindReviveSkill(revive.SkillId);
			if (revive.Attributed >= info->WvwTargets) { continue; }
			Window window = ReviveWindow(revive.SkillId);
			int64_t dt = static_cast<int64_t>(aTimeMs) - static_cast<int64_t>(revive.TimeMs);
			if (dt < window.From || dt > window.To) { continue; }
			int64_t distance = std::abs(dt - window.Expected);
			if (best == nullptr || distance < bestDistance) { best = &revive; bestDistance = distance; }
		}
		if (best)
		{
			Attribute(*best);
			up.Attributed = true;
		}
	}

	void Tracker::OnActivity(uint64_t aTimeMs, const std::string& aAccount)
	{
		auto it = m_Players.find(aAccount);
		if (it == m_Players.end() || it->second.Life != LifeState::Dead) { return; }
		it->second.Life = LifeState::Alive;
		it->second.LifeSinceMs = aTimeMs;
	}

	void Tracker::SetAway(uint64_t aTimeMs, const std::string& aAccount, bool aAway)
	{
		if (aAccount.empty()) { return; }
		PlayerStatus& player = GetPlayer(aAccount);
		if (player.Away == aAway) { return; }
		player.Away = aAway;
		if (!aAway)
		{
			player.Life = LifeState::Alive;
			player.LifeSinceMs = aTimeMs;
		}
	}

	void Tracker::MarkSeenSince(uint64_t aTimeMs, const std::string& aAccount)
	{
		if (aAccount.empty()) { return; }
		PlayerStatus& player = GetPlayer(aAccount);
		player.SeenSinceMs = aTimeMs;
		// What they did while we couldn't see them is unknown, so their skills count as unconfirmed again.
		for (SkillStatus& skill : player.Skills) { skill.Confirmed = false; }
	}

	void Tracker::MarkSkillReady(uint64_t aTimeMs, const std::string& aAccount, ReviveGroup aGroup)
	{
		if (aAccount.empty()) { return; }
		SkillStatus& skill = GetSkill(GetPlayer(aAccount), aGroup);
		skill.Confirmed = true;
		if (skill.State == SkillState::Casting) { return; }
		skill.State = SkillState::Ready;
		skill.ReadyAtMs = aTimeMs;
	}

	void Tracker::ForgetSkills(const std::string& aAccount)
	{
		auto it = m_Players.find(aAccount);
		if (it != m_Players.end()) { it->second.Skills.clear(); }
	}

	void Tracker::AddRevive(uint64_t aTimeMs, const std::string& aAccount, const ReviveSkill& aSkill)
	{
		Prune(aTimeMs);
		RecentRevive& revive = m_RecentRevives.emplace_back(RecentRevive{ aTimeMs, aAccount, aSkill.Id, aSkill.Group });

		Window window = ReviveWindow(aSkill.Id);
		for (RecentUp& up : m_RecentUps)
		{
			if (up.Attributed || revive.Attributed >= aSkill.WvwTargets) { continue; }
			int64_t dt = static_cast<int64_t>(up.TimeMs) - static_cast<int64_t>(aTimeMs);
			if (dt < window.From || dt > window.To) { continue; }
			Attribute(revive);
			up.Attributed = true;
		}
	}

	void Tracker::Attribute(RecentRevive& aRevive)
	{
		aRevive.Attributed++;
		SkillStatus& skill = GetSkill(GetPlayer(aRevive.Account), aRevive.Group);
		skill.Revived++;
		skill.LastRevived++;
	}

	void Tracker::Prune(uint64_t aTimeMs)
	{
		while (!m_RecentRevives.empty() && m_RecentRevives.front().TimeMs + kRecentWindowMs < aTimeMs) { m_RecentRevives.pop_front(); }
		while (!m_RecentUps.empty() && m_RecentUps.front().TimeMs + kRecentWindowMs < aTimeMs) { m_RecentUps.pop_front(); }
	}

	TurnView Tracker::GetTurn(uint64_t aNowMs) const
	{
		TurnView view;
		view.Rows.reserve(m_Order.size());

		for (const std::string& account : m_Order)
		{
			OrderRow& row = view.Rows.emplace_back();
			row.Account = account;
			row.Player = FindPlayer(account);
			row.IsLastUser = account == m_LastOrderedUser;
			if (row.Player == nullptr) { continue; } // never seen: assume ready

			const PlayerStatus& player = *row.Player;
			if (player.Away) { row.Status = Eligibility::Away; continue; }
			bool staleDowned = player.Life == LifeState::Downed && aNowMs > player.LifeSinceMs + kStaleDownedMs;
			if (player.Life == LifeState::Dead) { row.Status = Eligibility::Dead; continue; }
			if (player.Life == LifeState::Downed && !staleDowned) { row.Status = Eligibility::Downed; continue; }

			// A player is ready if any revive skill they have is ready (or they have none recorded yet).
			bool casting = false;
			bool anyReady = player.Skills.empty();
			bool confirmed = false;
			uint64_t soonestReady = UINT64_MAX;
			ReviveGroup soonestGroup = ReviveGroup::Count;
			for (const SkillStatus& skill : player.Skills)
			{
				bool stuck = skill.State == SkillState::Casting && aNowMs > skill.CastStartMs + kStuckCastMs;
				confirmed = confirmed || skill.Confirmed;
				if (skill.State == SkillState::Casting && !stuck) { casting = true; }
				else if (skill.State == SkillState::Ready || skill.ReadyAtMs <= aNowMs) { anyReady = true; }
				else if (skill.ReadyAtMs < soonestReady) { soonestReady = skill.ReadyAtMs; soonestGroup = skill.Group; }
			}
			if (casting) { row.Status = Eligibility::Casting; }
			else if (anyReady) { row.Status = Eligibility::Ready; }
			else
			{
				row.Status = Eligibility::Cooldown;
				row.ReadyInMs = soonestReady - aNowMs;
				// How long that recharge is in full, so the UI can show how far along it is.
				if (soonestGroup != ReviveGroup::Count)
				{
					row.RechargeMs = static_cast<uint64_t>(GetGroupInfo(soonestGroup).WvwRechargeS) * 1000;
				}
			}
			row.Unconfirmed = row.Status == Eligibility::Ready && !confirmed &&
				(player.SeenSinceMs == 0 || aNowMs < player.SeenSinceMs + kConfirmAfterMs);
		}

		if (view.Rows.empty()) { return view; }

		// Scan from the player after the last user; the last user themselves comes last.
		int count = static_cast<int>(view.Rows.size());
		auto last = std::find(m_Order.begin(), m_Order.end(), m_LastOrderedUser);
		int start = last == m_Order.end() ? 0 : static_cast<int>(last - m_Order.begin()) + 1;
		for (int i = 0; i < count; i++)
		{
			int index = (start + i) % count;
			Eligibility status = view.Rows[index].Status;
			if (status != Eligibility::Ready && status != Eligibility::Casting) { continue; }
			if (view.UpIndex < 0)
			{
				view.UpIndex = index;
				view.Rows[index].IsUp = true;
				continue;
			}
			view.BackupIndex = index;
			return view;
		}
		if (view.UpIndex >= 0) { return view; }

		for (int i = 0; i < count; i++)
		{
			const OrderRow& row = view.Rows[i];
			if (row.Status != Eligibility::Cooldown) { continue; }
			if (view.NextReadyIndex < 0 || row.ReadyInMs < view.Rows[view.NextReadyIndex].ReadyInMs) { view.NextReadyIndex = i; }
		}
		return view;
	}
}
