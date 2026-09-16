#include "Demo.h"

#include <memory>
#include <vector>

#include "FakeSquad.h"

namespace Rezz::Demo
{
	namespace
	{
		// One squad fight, looping. Times are seconds from the start of the loop.
		struct Beat
		{
			uint32_t AtS;
			enum class What : uint8_t { CombatStart, Use, Down, Up, Dead, CombatEnd } Does;
			int      Who; // index into the demo squad
		};

		constexpr uint32_t kLoopS = 100;

		constexpr Beat kFight[] = {
			{  2, Beat::What::CombatStart, 0 },
			{  6, Beat::What::Down,        2 },
			{  9, Beat::What::Use,         0 }, // the first in the order picks them up
			{ 12, Beat::What::Up,          2 },
			{ 20, Beat::What::Down,        4 },
			{ 24, Beat::What::Use,         1 },
			{ 27, Beat::What::Up,          4 },
			{ 38, Beat::What::Down,        5 },
			{ 44, Beat::What::Use,         3 }, // out of turn: the rotation carries on after them
			{ 47, Beat::What::Up,          5 },
			{ 58, Beat::What::Down,        1 },
			{ 66, Beat::What::Dead,        1 }, // nobody was ready in time
			{ 72, Beat::What::Use,         4 },
			{ 80, Beat::What::CombatEnd,   0 },
			{ 88, Beat::What::Up,          1 }, // rallied after the fight
		};

		// Names that cannot be mistaken for real players.
		struct Member
		{
			const char* Account;
			const char* Character;
			uint32_t    Profession;
			uint32_t    Elite;
		};

		constexpr Member kSquad[] = {
			{ ":Demo Firebrand.1001", "Demo Firebrand", 1, 62 },
			{ ":Demo Tempest.1002",   "Demo Tempest",   6, 48 },
			{ ":Demo Chrono.1003",    "Demo Chrono",    7, 40 },
			{ ":Demo Druid.1004",     "Demo Druid",     4, 5  },
			{ ":Demo Spellbreaker.1005", "Demo Spellbreaker", 2, 61 },
			{ ":Demo Scourge.1006",   "Demo Scourge",   8, 60 },
		};

		std::unique_ptr<Fake::Squad> s_Squad;
		std::vector<std::string>     s_Accounts;
		uint64_t                     s_LoopStartMs = 0;
		size_t                       s_NextBeat    = 0;
		std::string                  s_SelfAccount;
		bool                         s_Running     = false;

		// A fresh squad at the top of every loop, so the demo never drifts into a state nobody understands.
		void Restart(uint64_t aNowMs)
		{
			s_Squad = std::make_unique<Fake::Squad>(aNowMs);
			s_Accounts.clear();
			for (size_t i = 0; i < std::size(kSquad); i++)
			{
				// The player sees their own account where theirs would be in a real squad.
				bool self = i == 2;
				std::string account = self && !s_SelfAccount.empty() ? s_SelfAccount : kSquad[i].Account;
				std::string character = self && !s_SelfAccount.empty() ? DisplayAccount(account) : kSquad[i].Character;
				s_Squad->Add(account, character, kSquad[i].Profession, kSquad[i].Elite, self);
				s_Accounts.push_back(account);
			}
			s_Squad->Role(s_Accounts[0], SquadRole::Leader);
			s_Squad->Order(s_Accounts);
			s_LoopStartMs = aNowMs;
			s_NextBeat = 0;
		}
	}

	void Start(const std::string& aSelfAccount, uint64_t aNowMs)
	{
		s_SelfAccount = aSelfAccount;
		s_Running = true;
		Restart(aNowMs);
	}

	void Stop()
	{
		s_Running = false;
		s_Squad.reset();
	}

	bool Running() { return s_Running; }

	SessionView View(uint64_t aNowMs)
	{
		if (!s_Running) { return {}; }
		if (!s_Squad || aNowMs < s_LoopStartMs || aNowMs - s_LoopStartMs > kLoopS * 1000) { Restart(aNowMs); }

		uint64_t into = aNowMs - s_LoopStartMs;
		while (s_NextBeat < std::size(kFight) && kFight[s_NextBeat].AtS * 1000ull <= into)
		{
			const Beat& beat = kFight[s_NextBeat++];
			const std::string& who = s_Accounts[static_cast<size_t>(beat.Who)];
			switch (beat.Does)
			{
				case Beat::What::CombatStart: s_Squad->InCombat(); break;
				case Beat::What::Use:         s_Squad->Used(who, 0); break;
				case Beat::What::Down:        s_Squad->Downed(who); break;
				case Beat::What::Up:          s_Squad->Alive(who); break;
				case Beat::What::Dead:        s_Squad->Dead(who); break;
				case Beat::What::CombatEnd:   s_Squad->OutOfCombat(); break;
			}
		}

		s_Squad->SetNow(aNowMs);
		// Everyone keeps producing events, so nobody looks out of range while the demo runs.
		for (const std::string& account : s_Accounts) { s_Squad->Seen(account); }
		return s_Squad->View();
	}
}
