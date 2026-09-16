#include "Live.h"

#include <mutex>

#include <Windows.h>
#include <mmsystem.h>

#include "unofficial_extras/Definitions.h"

namespace Live
{
	namespace
	{
		std::mutex    s_Mutex;
		Rezz::Session s_Session;
	}

	void OnCombatSquad(const ArcDps::EvCombatData* aData)
	{
		if (aData == nullptr || aData->Ev == nullptr) { return; }
		uint8_t sc = aData->Ev->IsStatechange;
		// Cheap filter before taking the lock: only these statechanges matter to the session.
		if (sc != ArcDps::CBTS_ANIMATIONSTART && sc != ArcDps::CBTS_ANIMATIONSTOP && sc != ArcDps::CBTS_CHANGEDOWN &&
			sc != ArcDps::CBTS_CHANGEUP && sc != ArcDps::CBTS_CHANGEDEAD && sc != ArcDps::CBTS_BUFFAPPLY &&
			sc != ArcDps::CBTS_BUFFINITIAL && sc != ArcDps::CBTS_ENTERCOMBAT && sc != ArcDps::CBTS_SQCOMBATSTART &&
			sc != ArcDps::CBTS_SQCOMBATEND)
		{
			return;
		}
		uint32_t now = timeGetTime();
		std::scoped_lock lock(s_Mutex);
		s_Session.OnCombat(*aData, now);
	}

	void OnAgentUpdate(const ArcDps::EvAgentUpdate* aUpdate)
	{
		if (aUpdate == nullptr) { return; }
		uint32_t now = timeGetTime();
		std::scoped_lock lock(s_Mutex);
		s_Session.OnAgentUpdate(*aUpdate, now);
	}

	void OnSquadUpdate(const UserInfo* aUsers, uint64_t aCount)
	{
		if (aUsers == nullptr) { return; }
		uint32_t now = timeGetTime();
		std::scoped_lock lock(s_Mutex);
		for (uint64_t i = 0; i < aCount; i++)
		{
			const UserInfo& user = aUsers[i];
			if (user.AccountName == nullptr) { continue; }
			s_Session.OnRole(user.AccountName, static_cast<Rezz::SquadRole>(user.Role), user.Subgroup, now);
		}
	}

	void Tick()
	{
		uint32_t now = timeGetTime();
		std::scoped_lock lock(s_Mutex);
		s_Session.Tick(now);
	}

	void SetOrder(const std::vector<std::string>& aAccounts)
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.SetOrder(aAccounts);
	}

	Rezz::SessionView GetView()
	{
		uint32_t now = timeGetTime();
		std::scoped_lock lock(s_Mutex);
		return s_Session.GetView(now);
	}

	std::vector<Rezz::Notice> TakeNotices()
	{
		std::scoped_lock lock(s_Mutex);
		return s_Session.TakeNotices();
	}
}
