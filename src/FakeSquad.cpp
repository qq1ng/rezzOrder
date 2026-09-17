#include "FakeSquad.h"

#include <cstring>

#include "Arc.h"
#include "ReviveSkills.h"

namespace Rezz::Fake
{
	namespace
	{
		constexpr uint8_t kFullCast = ArcDps::ANIMSTOP_RETURN_CONTROL;
		// arcdps delivers squad events about this long after they happen.
		constexpr uint64_t kFeedDelayMs = 2650;
	}

	Squad::Squad(uint64_t aNowMs) : m_NowMs(aNowMs) {}

	const Squad::Known& Squad::Find(const std::string& aAccount) const
	{
		static const Known kMissing{};
		auto it = m_Known.find(aAccount);
		return it == m_Known.end() ? kMissing : it->second;
	}

	uint32_t Squad::ReviveSkillFor(uint32_t aProfession) const
	{
		switch (aProfession)
		{
			case 1: return 9163;  // Signet of Mercy
			case 2: return 14419; // Battle Standard
			case 4: return 12569; // Spirit of Nature
			case 6: return 5762;  // Glyph of Renewal (fire)
			case 7: return 10244; // Illusion of Life
			case 8: return 10611; // Signet of Undeath
			default: return 0;
		}
	}

	void Squad::Event(uint64_t aTimeMs, uint8_t aStatechange, const std::string& aAccount, uint32_t aSkill,
		uint8_t aResult, int32_t aBaseMs)
	{
		const Known& known = Find(aAccount);
		ArcDps::CombatEvent ev{};
		ev.Time = aTimeMs;
		ev.IsStatechange = aStatechange;
		ev.SrcAgent = known.Id;
		ev.SrcInstId = known.Instance;
		ev.SkillId = aSkill;
		ev.Result = aResult;
		ev.BuffDmg = aBaseMs;
		ArcDps::Agent src{ "", static_cast<uintptr_t>(known.Id), known.Profession, known.Elite, 0, 0 };
		ArcDps::EvCombatData data{ &ev, &src, nullptr, "", 0, 1 };
		m_Session.OnCombat(data, aTimeMs + kFeedDelayMs);
	}

	Squad& Squad::Add(const std::string& aAccount, const std::string& aCharacter, uint32_t aProfession, uint32_t aElite,
		bool aSelf)
	{
		// Three players to a subgroup, handed out in turn, so the squad isn't sorted by subgroup already.
		Known known{ m_NextId++, m_NextInstance++, aProfession, aElite, static_cast<uint16_t>(m_Known.size() % 3 + 1) };
		m_Known[aAccount] = known;

		ArcDps::EvAgentUpdate update{};
		strncpy_s(update.Account, aAccount.c_str(), _TRUNCATE);
		strncpy_s(update.Character, aCharacter.c_str(), _TRUNCATE);
		update.Id = static_cast<uintptr_t>(known.Id);
		update.InstanceId = known.Instance;
		update.Profession = aProfession;
		update.Elite = aElite;
		update.Added = 1;
		update.Self = aSelf ? 1 : 0;
		// Joined long enough ago that nobody counts as "just seen", which would make every state a guess.
		m_Session.OnAgentUpdate(update, At(300000));
		m_Session.OnRole(aAccount, SquadRole::Member, known.Subgroup, At(300000));
		return *this;
	}

	Squad& Squad::Role(const std::string& aAccount, SquadRole aRole)
	{
		m_Session.OnRole(aAccount, aRole, Find(aAccount).Subgroup, m_NowMs);
		return *this;
	}

	Squad& Squad::Used(const std::string& aAccount, uint64_t aMsAgo)
	{
		uint32_t skill = ReviveSkillFor(Find(aAccount).Profession);
		const ReviveSkill* info = FindReviveSkill(skill);
		int32_t cast = info ? static_cast<int32_t>(info->CastMs) : 2000;
		uint64_t start = At(aMsAgo);
		Event(start, ArcDps::CBTS_ANIMATIONSTART, aAccount, skill);
		Event(start + cast, ArcDps::CBTS_ANIMATIONSTOP, aAccount, skill, kFullCast, cast);
		return *this;
	}

	Squad& Squad::Casting(const std::string& aAccount, uint64_t aMsAgo)
	{
		Event(At(aMsAgo + 600), ArcDps::CBTS_ANIMATIONSTART, aAccount, ReviveSkillFor(Find(aAccount).Profession));
		return *this;
	}

	Squad& Squad::Downed(const std::string& aAccount, uint64_t aMsAgo)
	{
		Event(At(aMsAgo), ArcDps::CBTS_CHANGEDOWN, aAccount);
		return *this;
	}

	Squad& Squad::Alive(const std::string& aAccount, uint64_t aMsAgo)
	{
		Event(At(aMsAgo), ArcDps::CBTS_CHANGEUP, aAccount);
		return *this;
	}

	Squad& Squad::Illusion(const std::string& aAccount, uint64_t aMsAgo)
	{
		const Known& target = Find(aAccount);
		ArcDps::CombatEvent ev{};
		ev.Time = At(aMsAgo);
		ev.IsStatechange = ArcDps::CBTS_BUFFAPPLY;
		ev.DstAgent = target.Id;
		ev.DstInstId = target.Instance;
		ev.SkillId = kIllusionOfLifeEffect;
		ev.Value = 15000;
		ArcDps::Agent dst{ "", static_cast<uintptr_t>(target.Id), target.Profession, target.Elite, 0, 0 };
		ArcDps::EvCombatData data{ &ev, nullptr, &dst, "", 0, 1 };
		m_Session.OnCombat(data, ev.Time + kFeedDelayMs);
		return *this;
	}

	Squad& Squad::Dead(const std::string& aAccount, uint64_t aMsAgo)
	{
		Event(At(aMsAgo + 8000), ArcDps::CBTS_CHANGEDOWN, aAccount);
		Event(At(aMsAgo), ArcDps::CBTS_CHANGEDEAD, aAccount);
		return *this;
	}

	Squad& Squad::Seen(const std::string& aAccount, uint64_t aMsAgo)
	{
		Event(At(aMsAgo), ArcDps::CBTS_ENTERCOMBAT, aAccount);
		return *this;
	}

	Squad& Squad::LeftMap(const std::string& aAccount, uint64_t aMsAgo)
	{
		const Known& known = Find(aAccount);
		ArcDps::EvAgentUpdate update{};
		strncpy_s(update.Account, aAccount.c_str(), _TRUNCATE);
		update.Id = static_cast<uintptr_t>(known.Id);
		update.InstanceId = known.Instance;
		update.Profession = known.Profession;
		update.Elite = known.Elite;
		update.Added = 0;
		m_Session.OnAgentUpdate(update, At(aMsAgo));
		return *this;
	}

	Squad& Squad::InCombat(uint64_t aMsAgo)
	{
		if (m_Known.empty()) { return *this; }
		Event(At(aMsAgo), ArcDps::CBTS_SQCOMBATSTART, m_Known.begin()->first);
		return *this;
	}

	Squad& Squad::OutOfCombat(uint64_t aMsAgo)
	{
		if (m_Known.empty()) { return *this; }
		Event(At(aMsAgo), ArcDps::CBTS_SQCOMBATEND, m_Known.begin()->first);
		return *this;
	}

	Squad& Squad::Order(const std::vector<std::string>& aAccounts)
	{
		m_Session.SetOrder(aAccounts);
		return *this;
	}

	Squad& Squad::SetNow(uint64_t aNowMs)
	{
		m_NowMs = aNowMs;
		return *this;
	}

	SessionView Squad::View()
	{
		m_Session.Tick(m_NowMs);
		m_Session.TakeNotices();
		return m_Session.GetView(m_NowMs);
	}
}
