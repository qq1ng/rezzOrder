#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Arc.h"
#include "Session.h"

struct UserInfo;

// The addon's live revive order session. Event handlers may run on any thread; the UI reads copies.
namespace Live
{
	void OnCombatSquad(const ArcDps::EvCombatData* aData);
	void OnAgentUpdate(const ArcDps::EvAgentUpdate* aUpdate);
	void OnSquadUpdate(const UserInfo* aUsers, uint64_t aCount);

	void Tick();
	void SetOrder(const std::vector<std::string>& aAccounts);

	Rezz::SessionView GetView();
	std::vector<Rezz::Notice> TakeNotices();
}
