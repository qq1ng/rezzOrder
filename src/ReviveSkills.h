#pragma once

#include <cstdint>

// Instant revive skills, WvW values. Sources: wiki.guildwars2.com raw pages, api.guildwars2.com and the
// Phase 0 recordings (docs/phase0-results.md).

// Skills that share one cooldown on a player's bar. Glyph attunement variants are one glyph; the ranger
// spirit's slams are effects of the Spirit of Nature cast.
enum class ReviveGroup : uint8_t
{
	SignetOfMercy,
	GlyphOfRenewal,
	IllusionOfLife,
	SignetOfUndeath,
	BattleStandard,
	SpiritOfNature,
	Count
};

struct ReviveGroupInfo
{
	const char* Name;
	const char* Profession;
	uint16_t    WvwRechargeS; // base WvW recharge, before alacrity/chilled/traits
};

inline constexpr ReviveGroupInfo kReviveGroups[static_cast<int>(ReviveGroup::Count)] = {
	{ "Signet of Mercy",   "Guardian",     90  },
	{ "Glyph of Renewal",  "Elementalist", 90  },
	{ "Illusion of Life",  "Mesmer",       90  }, // confirmed in the field: 94 s shortest recast
	{ "Signet of Undeath", "Necromancer",  75  },
	{ "Battle Standard",   "Warrior",      120 },
	{ "Spirit of Nature",  "Ranger",       120 },
};

struct ReviveSkill
{
	uint32_t    Id;
	const char* Name;
	ReviveGroup Group;
	bool        IsPlayerCast; // false for effects cast by something else (the spirit's slams)
	uint8_t     WvwTargets;   // max downed allies revived in WvW
	uint16_t    CastMs;       // base activation time; a stop at or after this many base ms counts as used
};

inline constexpr ReviveSkill kReviveSkills[] = {
	{ 9163,  "Signet of Mercy",               ReviveGroup::SignetOfMercy,   true,  1, 2000 },
	{ 9243,  "Signet of Mercy (alt id)",      ReviveGroup::SignetOfMercy,   true,  1, 2000 },
	{ 5573,  "Glyph of Renewal",              ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 5762,  "Renewal of Fire",               ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 5763,  "Renewal of Water",              ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 5760,  "Renewal of Air",                ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 5761,  "Renewal of Earth",              ReviveGroup::GlyphOfRenewal,  true,  3, 2000 },
	{ 24407, "Renewal of Fire (underwater)",  ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 24410, "Renewal of Water (underwater)", ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 24409, "Renewal of Air (underwater)",   ReviveGroup::GlyphOfRenewal,  true,  1, 2000 },
	{ 24411, "Renewal of Earth (underwater)", ReviveGroup::GlyphOfRenewal,  true,  3, 2000 },
	{ 10244, "Illusion of Life",              ReviveGroup::IllusionOfLife,  true,  3, 1250 },
	{ 10611, "Signet of Undeath",             ReviveGroup::SignetOfUndeath, true,  1, 1500 },
	{ 14419, "Battle Standard",               ReviveGroup::BattleStandard,  true,  3, 2000 },
	{ 12569, "Spirit of Nature",              ReviveGroup::SpiritOfNature,  true,  2, 1500 },
	{ 12601, "Nature's Renewal",              ReviveGroup::SpiritOfNature,  false, 2, 750  }, // first slam
	{ 69336, "Nature's Renewal (2nd slam)",   ReviveGroup::SpiritOfNature,  false, 2, 750  }, // Nature's Vengeance trait
};

const ReviveSkill* FindReviveSkill(uint32_t aSkillId);
const ReviveGroupInfo& GetGroupInfo(ReviveGroup aGroup);

// Whether a stopped cast spent the skill. Rule from Phase 0 (docs/phase0-results.md: 0 contradictions over
// ~430 casts, incl. quickness and slow): arcdps reports a full cast (stop reason RETURN_CONTROL), or the cast
// ran for at least the base cast time. aBaseMs is cbtevent.buff_dmg on the stop event (speed-independent).
bool CountsAsUsed(const ReviveSkill& aSkill, uint8_t aStopReason, int32_t aBaseMs);

// True for skill/effect names that look revive-related (English client). Used only to widen logging
// so effect ids we don't know yet still get recorded.
bool IsReviveRelatedName(const char* aName);
