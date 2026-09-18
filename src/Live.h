#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Arc.h"
#include "Session.h"

struct UserInfo;
struct SquadMessageInfo;

// The addon's live revive order session. Event handlers may run on any thread; the UI reads copies.
namespace Live
{
	void OnCombatSquad(const ArcDps::EvCombatData* aData);
	void OnAgentUpdate(const ArcDps::EvAgentUpdate* aUpdate);
	void OnSquadUpdate(const UserInfo* aUsers, uint64_t aCount);
	void OnChatMessage(const SquadMessageInfo* aMessage);

	void Tick();
	void SetOrder(const std::vector<std::string>& aAccounts);
	void SetPrecast(const std::vector<std::string>& aAccounts);
	void SetBench(int aSubgroup);
	// Who may set our order by sharing one in squad chat.
	void SetShareRules(bool aFromLeaders, bool aFromAnyone);
	void SetAnswerRule(int aRule);
	// Bench swaps by subgroup, see Session::SetSubstitutes.
	void SetSubstitutes(bool aEnabled);
	void AcceptShare();
	void DismissShare();
	// One player's request has been answered; empty clears them all.
	void ClearRequest(const std::string& aAccount = {});

	Rezz::SessionView GetView();
	// This session's fights, oldest first. Copied, so the window can read it without holding the lock.
	std::vector<Rezz::FightStat> GetFights();
	std::vector<Rezz::Notice> TakeNotices();
}
