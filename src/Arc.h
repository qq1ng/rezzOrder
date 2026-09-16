#pragma once

#include <cstdint>

// arcdps event layouts and ids, taken from the evtc readme (reference/arcdps_evtc_README.txt, Sep 2026).
// The Arcdps Integration addon relays arcdps callbacks to Nexus unchanged, so these also describe
// the payloads of its EV_ARCDPS_* events. Its bundled ArcDPS.h predates the animation events, so we
// keep our own definitions.
namespace ArcDps
{
	struct CombatEvent
	{
		uint64_t Time; // timeGetTime() when arcdps registered the event
		uint64_t SrcAgent;
		uint64_t DstAgent;
		int32_t  Value;
		int32_t  BuffDmg;
		uint32_t OverstackValue;
		uint32_t SkillId;
		uint16_t SrcInstId;
		uint16_t DstInstId;
		uint16_t SrcMasterInstId;
		uint16_t DstMasterInstId;
		uint8_t  Iff;
		uint8_t  Buff;
		uint8_t  Result;
		uint8_t  IsActivation;
		uint8_t  IsBuffRemove;
		uint8_t  IsNinety;
		uint8_t  IsFifty;
		uint8_t  IsMoving;
		uint8_t  IsStatechange;
		uint8_t  IsFlanking;
		uint8_t  IsShields;
		uint8_t  IsOffcycle;
		uint8_t  Pad61;
		uint8_t  Pad62;
		uint8_t  Pad63;
		uint8_t  Pad64;
	};

	struct Agent
	{
		const char* Name; // character name for players, may be null, valid only during the callback
		uintptr_t   Id;
		uint32_t    Profession;
		uint32_t    Elite;
		uint32_t    IsSelf;
		uint16_t    Team;
	};

	// Payload of EV_ARCDPS_COMBATEVENT_SQUAD_RAW / EV_ARCDPS_COMBATEVENT_LOCAL_RAW.
	struct EvCombatData
	{
		CombatEvent* Ev; // null for agent add/remove/target notifications
		Agent*       Src;
		Agent*       Dst;
		const char*  SkillName;
		uint64_t     Id;
		uint64_t     Revision;
	};

	// Payload of EV_ARCDPS_SELF_JOIN / SELF_LEAVE / SQUAD_JOIN / SQUAD_LEAVE.
	struct EvAgentUpdate
	{
		char      Account[64];
		char      Character[64];
		uintptr_t Id;
		uintptr_t InstanceId;
		uint32_t  Added;
		uint32_t  Target;
		uint32_t  Self;
		uint32_t  Profession;
		uint32_t  Elite;
		uint16_t  Team;
		uint16_t  Subgroup;
	};

	enum StateChange : uint8_t
	{
		CBTS_COMBAT            = 0,
		CBTS_ENTERCOMBAT       = 1,
		CBTS_EXITCOMBAT        = 2,
		CBTS_CHANGEUP          = 3,
		CBTS_CHANGEDEAD        = 4,
		CBTS_CHANGEDOWN        = 5,
		CBTS_SQCOMBATSTART     = 9,
		CBTS_SQCOMBATEND       = 10,
		CBTS_BUFFINITIAL       = 18,
		CBTS_SPAWN             = 6,
		CBTS_DESPAWN           = 7,
		CBTS_WEAPSWAP          = 11,
		CBTS_MAPCHANGE         = 65,
		CBTS_ANIMATIONSTART    = 67,
		CBTS_ANIMATIONSTOP     = 68,
		CBTS_BUFFAPPLY         = 69,
		CBTS_BUFFCHANGE        = 70,
		CBTS_BUFFREMOVE_SINGLE = 71,
		CBTS_BUFFREMOVE_ALL    = 72,
	};

	// is_activation on CBTS_ANIMATIONSTOP
	enum Animation : uint8_t
	{
		ACTV_NONE    = 0,
		ACTV_MINIMUM = 3, // stopped after reaching the first trigger point / tooltip time -> skill went off
		ACTV_CANCEL  = 4, // stopped before reaching it -> skill did not go off
		ACTV_RESET   = 5, // animation completed fully
		ACTV_NODATA  = 6, // like ACTV_MINIMUM, but expected duration was 0/uncertain
	};

	// Why an animation stopped: cbtevent.result on CBTS_ANIMATIONSTOP (arcdps n_animationstop, "debug, subject to change").
	enum AnimationStop : uint8_t
	{
		ANIMSTOP_INTERRUPT      = 8,
		ANIMSTOP_COMMAND        = 12,
		ANIMSTOP_MOVEDODGE      = 14,
		ANIMSTOP_RETURN_CONTROL = 22, // seen on every full cast in Phase 0
	};

	const char* StateChangeName(uint8_t aStateChange);
	const char* AnimationName(uint8_t aActivation);
	const char* AnimationStopName(uint8_t aReason);
	const char* ProfessionName(uint32_t aProfession);

	// arcdps' own reading of an animation stop. Wrong for several revive skills; see ReviveSkills.h.
	const char* CastVerdict(uint8_t aActivation);
}
