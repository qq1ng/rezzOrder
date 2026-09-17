#include "Live.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#include <Windows.h>
#include <mmsystem.h>

#include "Share.h"
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
		// Cheap filter before taking the lock: only these statechanges matter to the session. Buff removals are
		// frequent, so of those only the end of Illusion of Life gets through.
		bool illusionEnds = (sc == ArcDps::CBTS_BUFFREMOVE_ALL || sc == ArcDps::CBTS_BUFFREMOVE_SINGLE) &&
			aData->Ev->SkillId == kIllusionOfLifeEffect;
		if (!illusionEnds && sc != ArcDps::CBTS_ANIMATIONSTART && sc != ArcDps::CBTS_ANIMATIONSTOP && sc != ArcDps::CBTS_CHANGEDOWN &&
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

	void OnChatMessage(const SquadMessageInfo* aMessage)
	{
		if (aMessage == nullptr || aMessage->AccountName == nullptr || aMessage->Text == nullptr) { return; }
		std::string account = aMessage->AccountName;
		// The text is null terminated, so its real length is measured rather than taken on trust: a wrong
		// TextLength would otherwise read past the end of somebody else's buffer.
		size_t length = ::strnlen(aMessage->Text, Rezz::Share::kMaxTextChars);
		if (aMessage->TextLength > 0 && static_cast<size_t>(aMessage->TextLength) < length)
		{
			length = static_cast<size_t>(aMessage->TextLength);
		}
		std::string text(aMessage->Text, length);
		uint32_t now = timeGetTime();
		std::scoped_lock lock(s_Mutex);
		s_Session.OnChatMessage(account, text, now);
	}

	void SetShareRules(bool aFromLeaders, bool aFromAnyone)
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.SetShareRules(aFromLeaders, aFromAnyone);
	}

	void SetSubstitutes(bool aEnabled)
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.SetSubstitutes(aEnabled);
	}

	void SetAnswerRule(int aRule)
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.SetAnswerRule(static_cast<Rezz::Session::AnswerRule>(aRule));
	}

	void AcceptShare()
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.AcceptShare();
	}

	void DismissShare()
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.DismissShare();
	}

	void ClearRequest()
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.ClearRequest();
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

	void SetBench(int aSubgroup)
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.SetBench(static_cast<uint16_t>(std::clamp(aSubgroup, 0, 255)));
	}

	void SetPrecast(const std::vector<std::string>& aAccounts)
	{
		std::scoped_lock lock(s_Mutex);
		s_Session.SetPrecast(aAccounts);
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
