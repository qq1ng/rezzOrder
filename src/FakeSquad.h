#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Session.h"

// A squad that never existed: players, casts, downs and map changes fed into a real Rezz::Session as arcdps
// events. Demo mode (src/Demo.cpp) and the offscreen render harness (tools/uishot) both build their squads
// with this, so what they show is produced by exactly the code that handles a real squad.
//
// Every event happens at "now" unless it is given a number of milliseconds ago. Setting "now" forward lets a
// squad be played out over time, which is what demo mode does.
namespace Rezz::Fake
{
	// One squad member, before anything happens to them.
	struct Player
	{
		std::string Account;
		std::string Character;
		uint32_t    Profession;
		uint32_t    Elite;
	};

	class Squad
	{
	public:
		explicit Squad(uint64_t aNowMs = 600000);

		// Adds a squad member. aSelf marks the one we are playing.
		Squad& Add(const std::string& aAccount, const std::string& aCharacter, uint32_t aProfession, uint32_t aElite,
			bool aSelf = false);
		// aRole: the Unofficial Extras role, for the [Lead]/[Lt] markers in the editor.
		Squad& Role(const std::string& aAccount, SquadRole aRole);

		// A full cast of their revive skill.
		Squad& Used(const std::string& aAccount, uint64_t aMsAgo = 0);
		// A cast that is still running.
		Squad& Casting(const std::string& aAccount, uint64_t aMsAgo = 0);
		Squad& Downed(const std::string& aAccount, uint64_t aMsAgo = 0);
		Squad& Alive(const std::string& aAccount, uint64_t aMsAgo = 0);
		Squad& Dead(const std::string& aAccount, uint64_t aMsAgo = 0);
		// Illusion of Life put on them: 15 s from then.
		Squad& Illusion(const std::string& aAccount, uint64_t aMsAgo = 0);
		// Any event from them, so they don't look out of range.
		Squad& Seen(const std::string& aAccount, uint64_t aMsAgo = 0);
		// They left our map.
		Squad& LeftMap(const std::string& aAccount, uint64_t aMsAgo = 0);
		// The squad is fighting; players with no event since then show the out-of-range marker.
		Squad& InCombat(uint64_t aMsAgo = 0);
		Squad& OutOfCombat(uint64_t aMsAgo = 0);

		Squad& Order(const std::vector<std::string>& aAccounts);

		// Moves the clock; later events happen at the new time.
		Squad& SetNow(uint64_t aNowMs);
		uint64_t Now() const { return m_NowMs; }

		SessionView View();
		Rezz::Session& Session() { return m_Session; }

	private:
		struct Known
		{
			uint64_t Id;
			uint16_t Instance;
			uint32_t Profession;
			uint32_t Elite;
			uint16_t Subgroup;
		};

		const Known& Find(const std::string& aAccount) const;
		void Event(uint64_t aTimeMs, uint8_t aStatechange, const std::string& aAccount, uint32_t aSkill = 0,
			uint8_t aResult = 0, int32_t aBaseMs = 0);
		uint64_t At(uint64_t aMsAgo) const { return aMsAgo > m_NowMs ? 0 : m_NowMs - aMsAgo; }
		uint32_t ReviveSkillFor(uint32_t aProfession) const;

		Rezz::Session                          m_Session;
		std::unordered_map<std::string, Known> m_Known;
		uint64_t                               m_NowMs;
		uint64_t                               m_NextId       = 100;
		uint16_t                               m_NextInstance = 10;
	};
}
