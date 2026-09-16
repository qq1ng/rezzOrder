#include "Arc.h"

#include <iterator>

namespace ArcDps
{
	const char* StateChangeName(uint8_t aStateChange)
	{
		switch (aStateChange)
		{
			case CBTS_COMBAT:            return "COMBAT";
			case CBTS_ENTERCOMBAT:       return "ENTERCOMBAT";
			case CBTS_EXITCOMBAT:        return "EXITCOMBAT";
			case CBTS_CHANGEUP:          return "CHANGEUP";
			case CBTS_CHANGEDEAD:        return "CHANGEDEAD";
			case CBTS_CHANGEDOWN:        return "CHANGEDOWN";
			case CBTS_SPAWN:             return "SPAWN";
			case CBTS_DESPAWN:           return "DESPAWN";
			case CBTS_WEAPSWAP:          return "WEAPSWAP";
			case CBTS_MAPCHANGE:         return "MAPCHANGE";
			case CBTS_ANIMATIONSTART:    return "ANIMATIONSTART";
			case CBTS_ANIMATIONSTOP:     return "ANIMATIONSTOP";
			case CBTS_BUFFAPPLY:         return "BUFFAPPLY";
			case CBTS_BUFFCHANGE:        return "BUFFCHANGE";
			case CBTS_BUFFREMOVE_SINGLE: return "BUFFREMOVE_SINGLE";
			case CBTS_BUFFREMOVE_ALL:    return "BUFFREMOVE_ALL";
			default:                     return "";
		}
	}

	const char* AnimationName(uint8_t aActivation)
	{
		switch (aActivation)
		{
			case ACTV_NONE:    return "NONE";
			case ACTV_MINIMUM: return "MINIMUM";
			case ACTV_CANCEL:  return "CANCEL";
			case ACTV_RESET:   return "RESET";
			case ACTV_NODATA:  return "NODATA";
			default:           return "UNKNOWN";
		}
	}

	const char* AnimationStopName(uint8_t aReason)
	{
		static const char* names[] = {
			"NONE", "INSTANT", "MULTI", "TRANSITION", "PARTIAL", "ENDED", "CANCEL", "STOWDRAW", "INTERRUPT",
			"DEATH", "DOWNED", "CROWDCONTROL", "COMMAND", "MOTIONSKILL", "MOVEDODGE", "MOTIONSKILL_VIA_RESET",
			"MOVESKILL", "STOW", "ANY", "GADGET_VIA_RESET", "MANUAL_EXPIRY", "DESPAWN", "RETURN_CONTROL",
			"READY", "INVISIBLE", "PICKUP"
		};
		return aReason < std::size(names) ? names[aReason] : "UNKNOWN";
	}

	const char* ProfessionName(uint32_t aProfession)
	{
		static const char* names[] = {
			"Unknown", "Guardian", "Warrior", "Engineer", "Ranger", "Thief",
			"Elementalist", "Mesmer", "Necromancer", "Revenant"
		};
		return aProfession < std::size(names) ? names[aProfession] : "Unknown";
	}

	const char* CastVerdict(uint8_t aActivation)
	{
		switch (aActivation)
		{
			case ACTV_MINIMUM: return "USED";
			case ACTV_RESET:   return "USED (full)";
			case ACTV_NODATA:  return "USED? (no data)";
			case ACTV_CANCEL:  return "CANCELLED";
			default:           return "?";
		}
	}
}
