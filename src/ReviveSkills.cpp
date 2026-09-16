#include "ReviveSkills.h"

#include <string_view>

#include "Arc.h"

bool CountsAsUsed(const ReviveSkill& aSkill, uint8_t aStopReason, int32_t aBaseMs)
{
	return aStopReason == ArcDps::ANIMSTOP_RETURN_CONTROL || aBaseMs >= aSkill.CastMs;
}

const ReviveSkill* FindReviveSkill(uint32_t aSkillId)
{
	for (const ReviveSkill& skill : kReviveSkills)
	{
		if (skill.Id == aSkillId) { return &skill; }
	}
	return nullptr;
}

const ReviveGroupInfo& GetGroupInfo(ReviveGroup aGroup)
{
	return kReviveGroups[static_cast<int>(aGroup)];
}

bool IsReviveRelatedName(const char* aName)
{
	if (aName == nullptr || aName[0] == '\0') { return false; }

	static constexpr std::string_view keywords[] = {
		"Mercy", "Renewal", "Illusion of Life", "Undeath", "Battle Standard", "Spirit of Nature"
	};
	std::string_view name(aName);
	for (std::string_view keyword : keywords)
	{
		if (name.find(keyword) != std::string_view::npos) { return true; }
	}
	return false;
}
