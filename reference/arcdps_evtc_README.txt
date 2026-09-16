writeencounter.cpp has code used to write evtc files.
check file header - evtc bytes for filetype, arcdps build yyyymmdd for compatibility, target species id for boss.
an npcid of 1 indicates log is wvw.
an npcid of 2 indicates log is map
nameless/combatless agents may not be written to table while appearing in events.
skill and agent names use utf8 encoding.
evtc_agent.name is a combo string on players - character name <null> account name <null> subgroup str literal <null>.
add u16 field to the agent table, agents[x].instance_id, initialized to 0.
add u64 fields agents[x].first_aware initialized to 0, and agents[x].last_aware initialized to u64max.
add u64 field agents[x].master_addr, initialized to 0.
if evtc_agent.is_elite == 0xffffffff && upper half of evtc_agent.prof == 0xffff, agent is a gadget with pseudo id as lower half of evtc_agent.prof (volatile id).
if evtc_agent.is_elite == 0xffffffff && upper half of evtc_agent.prof != 0xffff, agent is a npc with species id as lower half of evtc_agent.prof (reliable id).
if evtc_agent.is_elite != 0xffffffff, agent is a player with profession as evtc_agent.prof and elite spec as evtc_agent.is_elite.
gadgets do not have true ids and are generated through a combination of gadget parameters - they will collide with npcs and should be treated separately.
iterate through all events, assigning instance ids and first/last aware ticks.
set agents[x].instance_id = src_instid where agents[x].addr == src_agent && !is_statechange.
set agents[x].first_aware = time on first event, then all consecutive event times to agents[x].last_aware.
iterate through all events again, this time assigning master agent.
set agents[z].master_agent on encountering src_master_instid != 0.
agents[z].master_addr = agents[x].addr where agents[x].instance_id == src_master_instid && agent[x].first_aware < time < last_aware.
iterate through all events one last time, this time parsing for the data you want.
src_agent and/or dst_agent should be used to associate event data with local data.

---

common to events with agents:
time - timeGetTime() at time of registering the event (note: some events use this for other data and not time, this will be indicated).
src_instid - id of agent as appears in game at time of event. out-of-range agents may have this set even when src_agent is zero.
dst_instid - id of agent as appears in game at time of event. out-of-range agents may have this set even when dst_agent is zero.
src_master_instid - if src_agent has a master (eg. is minion), will be equal to instid of master, zero otherwise.
dst_master_instid - if dst_agent has a master (eg. is minion), will be equal to instid of master, zero otherwise.

event types (is_statechange):
enum cbtstatechange {
	CBTS_COMBAT = 0, // combat events
	// src_agent: source agent
	// dst_agent: target agent
	// value: combined shield+health strike damage
	// buff_dmg: combined shield+health buff damage
	// overstack_value: shield damage
	// skillid: damage skill id
	// iff: is friend foe of enum iff
	// is_buff: skill is a buff
	// result: combat result of enum cbtresult
	// is_ninety: src is above 90% health
	// is_fifty: dst is below 50% health
	// is_moving: bit0 set if src is moving, bit1 set if dst is moving
	// is_flanking: src is flanking dst
	// is_shields: damage was partially or wholly absorbed by barrier
	// is_offcycle: dst was downed at time of event
	// evtc: limited to squad outside instances
	// realtime: limited to squad

	CBTS_ENTERCOMBAT, // agent entered combat
	// src_agent: relates to agent
	// dst_agent: subgroup
	// value: prof id
	// buff_dmg: elite spec id
	// evtc: limited to squad outside instances
	// realtime: limited to squad

	CBTS_EXITCOMBAT, // agent left combat
	// src_agent: relates to agent
	// dst_agent: subgroup
	// value: prof id
	// buff_dmg: elite spec id
	// evtc: limited to squad outside instances
	// realtime: limited to squad

	CBTS_CHANGEUP, // agent is alive at time of event
	// src_agent: relates to agent
	// evtc: limited to agent table outside instances
	// realtime: limited to squad

	CBTS_CHANGEDEAD, // agent is dead at time of event
	// src_agent: relates to agent
	// evtc: limited to agent table outside instances
	// realtime: limited to squad

	CBTS_CHANGEDOWN, // agent is down at time of event
	// src_agent: relates to agent
	// evtc: limited to agent table outside instances
	// realtime: limited to squad

	CBTS_SPAWN, // agent entered tracking
	// src_agent: relates to agent
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_DESPAWN, // agent left tracking
	// src_agent: relates to agent
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_HEALTHPCTUPDATE, // agent health percentage changed
	// src_agent: relates to agent
	// dst_agent: percent * 10000 eg. 99.5% will be 9950
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_SQCOMBATSTART, // squad combat start, first player enter combat. previously named log start
	// value: as uint32_t, server unix timestamp
	// buff_dmg: local unix timestamp
	// evtc: yes
	// realtime: yes

	CBTS_SQCOMBATEND, // squad combat stop, last player left combat. previously named log end
	// dst_agent: bit 0 = log ended by pov map exit
	// value: as uint32_t, server unix timestamp
	// buff_dmg: local unix timestamp
	// evtc: yes
	// realtime: yes

	CBTS_WEAPSWAP, // agent weapon set changed
	// src_agent: relates to agent
	// dst_agent: new weapon set id
	// value: old weapon seet id
	// evtc: yes
	// realtime: yes

	CBTS_MAXHEALTHUPDATE, // agent maximum health changed
	// src_agent: relates to agent
	// dst_agent: new max health
	// evtc: limited to non-players
	// realtime: no

	CBTS_POINTOFVIEW, // "recording" player
	// src_agent: relates to agent
	// evtc: yes
	// realtime: no

	CBTS_LANGUAGE, // text language id
	// src_agent: text language id
	// evtc: yes
	// realtime: no

	CBTS_GWBUILD, // game build
	// src_agent: game build number
	// evtc: yes
	// realtime: no

	CBTS_SHARDID, // server shard id
	// src_agent: shard id
	// evtc: yes
	// realtime: no

	CBTS_REWARD, // wiggly box reward
	// dst_agent: reward id
	// value: reward type
	// evtc: yes
	// realtime: yes

	CBTS_BUFFINITIAL, // buff application for buffs already existing at time of event
	// matches CBTS_BUFFAPPLY, except
	// buff_dmg: original ms duration of stack
	// evtc: limited to squad outside instances
	// realtime: limited to squad

	CBTS_POSITION, // agent position changed
	// src_agent: relates to agent
	// dst_agent: (float*)&dst_agent is float[3], x/y/z
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_VELOCITY, // agent velocity changed
	// src_agent: relates to agent
	// dst_agent: (float*)&dst_agent is float[3], x/y/z
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_FACING, // agent facing direction changed
	// src_agent: relates to agent
	// dst_agent: (float*)&dst_agent is float[2], x/y
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_TEAMCHANGE, // agent team id changed
	// src_agent: relates to agent
	// dst_agent: new team id
	// value: old team id
	// evtc: limited to agent table outside instances
	// realtime: limited to squad

	CBTS_ATTACKTARGET, // attacktarget to gadget association
	// src_agent: relates to agent, the attacktarget
	// dst_agent: the gadget
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_TARGETABLE, // agent targetable state. 0 false, 1 true, 2 unsupported
	// src_agent: relates to agent
	// dst_agent: new targetable state (attacktargets if selectable, gadgets if interactable or healthbar shown, characters if healthbar shown)
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_MAPID, // map info
	// src_agent: map id
	// dst_agent: map type
	// evtc: yes
	// realtime: no

	CBTS_REPLINFO, // internal use
	// internal use

	CBTS_BUFFACTIVE, // buff instance is now active
	// src_agent: relates to agent
	// dst_agent: trackable id
	// value: current buff duration
	// evtc: limited to squad outside instances
	// realtime: limited to squad

	CBTS_BUFFDEACTIVE, // buff set inactive
	// src_agent: relates to agent
	// value: new duration
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to squad outside instances
	// realtime: limited to squad

	CBTS_GUILD, // agent is a member of guild
	// src_agent: relates to agent
	// dst_agent: (uint8_t*)&dst_agent is uint8_t[16], guid of guild
	// value: new duration
	// evtc: limited to squad outside instances
	// realtime: no

	CBTS_BUFFINFO, // buff information. logs will always contain info for skill ids in skillid_mask below
	// overstack_value: max combined duration
	// skillid: skilldef id of buff
	// src_master_instid: stacking limit
	// is_src_flanking: likely an invuln
	// is_shields: likely an invert
	// is_offcycle: category
	// pad61: buff stacking type
	// pad62: likely a resistance
	// pad63: non-zero if used in buff damage simulation (rough pov-only check)
	// evtc: yes
	// realtime: no

	CBTS_BUFFFORMULA, // buff formula, one per event of this type. see bottom of this file for list of always-included skills
	// time: (float*)&time is float[9], type attribute1 attribute2 parameter1 parameter2 parameter3 trait_condition_source trait_condition_self content_reference
	// skillid: skilldef id of buff
	// src_instid: (float*)&src_instid is float[2], buff_condition_source buff_condition_self
	// evtc: yes
	// realtime: no

	CBTS_SKILLINFO, // skill information
	// time: (float*)&time is float[4], cost range0 range1 tooltiptime
	// skillid: skilldef id of skill
	// evtc: yes
	// realtime: no

	CBTS_SKILLTIMING, // skill timing, one per event of this type
	// src_agent: timing type
	// dst_agent: at time since activation in milliseconds
	// skillid: skilldef id of skill
	// evtc: yes
	// realtime: no

	CBTS_DEFIANCEBARSTATE, // agent defiance bar state changed
	// src_agent: relates to agent
	// dst_agent: new breakbar state
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_DEFIANCEBARPERCENT, // agent defiance bar percentage changed
	// src_agent: relates to agent
	// value: (float*)&value is float[1], new percentage
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_INTEGRITY, // one event per message. previously named error
	// time: (char*)&time is char[32], a short null-terminated message with reason
	// evtc: yes
	// realtime: no

	CBTS_MARKER, // one event per marker on an agent
	// src_agent: relates to agent
	// value: markerdef id. if value is 0, remove all markers presently on agent
	// buff: marker is a commander tag
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_BARRIERPCTUPDATE, // agent barrier percentage changed
	// src_agent: relates to agent
	// dst_agent: percent * 10000 eg. 99.5% will be 9950
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_STATRESET_DEFUNC, // retired, not used since 260402+
	// retired

	CBTS_EXTENSION, // for extension use. not managed by arcdps
	// evtc: yes
	// realtime: yes

	CBTS_APIDELAYED_DEFUNC, // retired, not used since 260501+
	// retired

	CBTS_INSTANCESTART, // map instance start
	// src_agent: milliseconds ago instance was started
	// value: *(uint32_t*)&value server socket
	// evtc: yes
	// realtime: no

	CBTS_RATEHEALTH, // retired, not used since 260627+
	// retired

	CBTS_LAST90BEFOREDOWN_DEFUNC, // retired, not used since 240529+
	// retired

	CBTS_EFFECT1_DEFUNC, // retired, not used since 230716+
	// retired

	CBTS_IDTOGUID, // content id to guid association for volatile types. types may contain additional information, see n_contentlocal
	// src_agent: (uint8_t*)&src_agent is uint8_t[16] guid of content
	// overstack_value: is of enum contentlocal
	// evtc: yes
	// realtime: no

	CBTS_LOGNPCUPDATE, // log boss agent changed
	// src_agent: species id of agent
	// dst_agent: related to agent
	// value: as uint32_t, server unix timestamp
	// evtc: yes
	// realtime: yes

	CBTS_IDLEEVENT, // internal use
	// internal use

	CBTS_EXTENSIONCOMBAT, // for extension use. not managed by arcdps
	// assumed to be cbtevent struct, skillid will be processed as such for purpose of buffinfo/skillinfo
	// evtc: yes
	// realtime: yes

	CBTS_FRACTALSCALE, // fractal scale for fractals
	// src_agent: scale
	// evtc: yes
	// realtime: no

	CBTS_EFFECT2_DEFUNC, // retired, not used since 250526+
	// retired

	CBTS_RULESET, // ruleset for self
	// src_agent: bit0: pve, bit1: wvw, bit2: pvp
	// evtc: yes
	// realtime: no

	CBTS_SQUADMARKER_GROUND, // squad ground markers
	// src_agent: (float*)&src_agent is float[3], x/y/z of marker location. if values are all zero or infinity, this marker is removed
	// skillid: index of marker eg. 0 is arrow
	// evtc: yes
	// realtime: no

	CBTS_ARCBUILD, // arc build info
	// src_agent: (char*)&src_agent is a null-terminated string matching the full build string in arcdps.log
	// evtc: yes
	// realtime: no

	CBTS_GLIDER, // glider status change
	// src_agent: related to agent
	// value: 1 deployed, 0 stowed
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_STUNBREAK, // disable stopped early
	// src_agent: related to agent
	// value: duration remaining
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_MISSILECREATE, // create a missile
	// src_agent: related to agent
	// value: (int16*)&value is int16[3], location x/y/z, divided by 10
	// overstack_value: skin id (player only)
	// skillid: missile skill id
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_MISSILELAUNCH, // launch missile
	// src_agent: related to agent
	// dst_agent: at agent, if set and in range
	// value: (int16*)&value is int16[6], target x/y/z, current x/y/z, divided by 10
	// skillid: missile skill id
	// iff: (uint8_t*)&iff is uint8_t[1], launch motion. unknown, from client
	// result: (int16_t*)&result is int16[1], motion radius
	// is_buffremove: (uint32_t*)&is_buffremove is uint32_t[1], launch flags. unknown, from client
	// is_src_flanking: non-zero if first launch
	// is_shields: (int16_t*)&is_shields is int16[1], missile speed
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_MISSILEREMOVE, // remove missile
	// src_agent: related to agent
	// value: friendly fire damage total
	// skillid: missile skill id
	// buff_dmg: (int16*)&value is int16[3], location x/y/z, divided by 10
	// is_src_flanking: hit at least one enemy along the way
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_EFFECTGROUNDCREATE, // play effect on ground
	// src_agent: related to agent
	// dst_agent: (int16*)&dst_agent is int16[6], origin x/y/z divided by 10, orient x/y/z multiplied by 1000
	// skillid: effect id (prefer using an id to guid map via n_contentlocal)
	// iff: (uint32_t*)&iff is uint32_t[1], effect duration. if duration is zero, it may be a fixed length duration (see n_contentlocal)
	// is_buffremove: flags
	// is_flanking: effect is on a non-static platform
	// is_shields: (int16_t*)&is_shields is int16[1], scale (if zero, assume 1) multiplied by 1000
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_EFFECTGROUNDREMOVE, // stop effect on ground
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: yes
	// realtime: no

	CBTS_EFFECTAGENTCREATE, // play effect around agent
	// src_agent: related to agent
	// skillid: effect id (prefer using an id to guid map via n_contentlocal)
	// iff: (uint32_t*)&iff is uint32_t[1], effect duration. if duration is zero, it may be a fixed length duration (see n_contentlocal)
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_EFFECTAGENTREMOVE, // stop effect around agent
	// src_agent: related to agent
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_IIDCHANGE, // iid (previously evtc_agent->addr) changed. players only, happens after spawn when player historical data is loaded. does not happen if player has no historical data
	// src_agent: old iid
	// dst_agent: new iid
	// evtc: yes
	// realtime: no

	CBTS_MAPCHANGE, // map changed
	// src_agent: new map id
	// dst_agent: old map id
	// value: new map type
	// evtc: yes
	// realtime: yes

	CBTS_EARLYEXIT, // internal use
	// internal use

	CBTS_ANIMATIONSTART, // animation start
	// src_agent: agent beginning animation
	// dst_agent: target agent if applicable
	// value: ms duration until minimum of last significant trigger point and tooltip time
	// buff_dmg: ms duration when control is returned to agent
	// overstack_value: reference data (see CSK enum)
	// skillid: skill id
	// evtc: yes
	// realtime: yes

	CBTS_ANIMATIONSTOP, // animation stop
	// src_agent: agent beginning animation
	// value: ms duration spent in animation scaled for speed
	// buff_dmg: ms duration spent in animation not scaled
	// skillid: skill id of previous animation start
	// is_activation: simple progress check from cbtanimation
	// evtc: yes
	// realtime: yes

	CBTS_BUFFAPPLY, // buff stack application
	// src_agent: agent applying the stack
	// dst_agent: agent the stack was applied to
	// value: ms duration applied
	// skillid: buff skill id
	// iff: is friend foe of enum iff
	// is_ninety: src is above 90% health
	// is_fifty: dst is below 50% health
	// is_moving: bit0 set if src is moving, bit1 set if dst is moving
	// is_flanking: src is flanking dst
	// is_shields: non-zero if buff is active when applied
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: yes
	// realtime: limited to visible, must have previous squad to squad application

	CBTS_BUFFCHANGE, // buff stack duration change, active only
	// dst_agent: relates to agent
	// value: duration difference
	// overstack_value: new ms duration
	// skillid: buff skill id
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: yes
	// realtime: limited to visible, must have previous squad to squad application

	CBTS_BUFFREMOVE_SINGLE, // buff stack removed
	// src_agent: agent with buff removed
	// dst_agent: agent removing the buff
	// value: ms duration removed
	// skillid: buff skill id
	// iff: is friend foe of enum iff
	// is_buffremove: of enum cbtbuffremove
	// is_ninety: src is above 90% health
	// is_fifty: dst is below 50% health
	// is_moving: bit0 set if src is moving, bit1 set if dst is moving
	// is_flanking: src is flanking dst
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: yes
	// realtime: limited to visible, must have previous squad to squad application

	CBTS_BUFFREMOVE_ALL, // all buff stacks of skillid removed
	// src_agent: agent with buffs removed
	// dst_agent: agent removing the buffs
	// value: ms duration removed calculated as duration
	// buff_dmg: ms duration removed calculated as intensity
	// skillid: buff skill id
	// iff: is friend foe of enum iff
	// is_buffremove: of enum cbtbuffremove
	// is_ninety: src is above 90% health
	// is_fifty: dst is below 50% health
	// is_moving: bit0 set if src is moving, bit1 set if dst is moving
	// is_flanking: src is flanking dst
	// evtc: yes
	// realtime: limited to visible, must have previous squad to squad application

	CBTS_TRANSFORMATION, // transformation
	// src_agent: related to agent
	// skillid: transformation id (0 if untransformed)
	// value: duration
	// is_shields: undocumented server data
	// is_offcycle: undocumented server data
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_WVWTEAMS, // wvw team association
	// src_agent: (uint32_t*)&src_agent is uint32[6], redshard id, blueshard id, greenshard id, redteam id, blueteam id, greenteam id
	// evtc: yes
	// realtime: no

	CBTS_WVWOBJECTIVESTATUS, //  status update on wvw objectives
	// value: map id
	// buff_dmg: team id
	// skillid: objective id
	// buff: objective type
	// pad61: (uint32_t*)&pad61 is uint32[1], upgrade progress count
	// evtc: yes
	// realtime: yes

	CBTS_STEALTHCHANGE, // agent stealth state. 0 false, 1 true, 2 unsupported
	// src_agent: relates to agent
	// dst_agent: new stealth state (characters only)
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_GADGETANIMATION, // play model animation
	// src_agent: relates to agent
	// dst_agent: token
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_GADGETNAME, // gadget name visibility state. 0 false, 1 true, 2 unsupported
	// src_agent: relates to agent
	// dst_agent: new state (gadgets only)
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_MISSILEEFFECT, // apply effect to missile
	// dst_agent: owner of missile
	// skillid: effect id
	// value: *(uint32_t*)&value duration
	// pad61: (uint32_t*)&pad61 is uint32[1], trackable id
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_GADGETCAPTUREOUTLINESHOW, // gadget capture point show outline
	// src_agent: relates to agent
	// buff: wrbg colour
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_GADGETCAPTURESPLITPERCENT, // gadget capture point percent split
	// src_agent: relates to agent
	// value: *(float*)&ev->value percent (1.0 - 0.0)
	// buff: wrbg capping from
	// result: wrbg capping by
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_GADGETCAPTUREOUTLINEHIDE, // gadget capture point hide outline
	// src_agent: relates to agent
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_GADGETCAPTUREOUTLINEPOINT, // gadget capture point point data
	// src_agent: relates to agent
	// dst_agent: point index
	// value: *(float*)&ev->value x
	// buff_dmg: *(float*)&ev->buff_dmg y
	// overstack_value: point count for this agent. if count is 1, shape is a circle around agent with radius x
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_TICK, // tick, every 25 ticks
	// src_agent: current extrapolated tick (ticks may go backwards if real update is lower than extrapolation)
	// dst_agent: ticks since last real tick update
	// value: ping at time of event (updated by the client once every 10s)
	// evtc: yes
	// realtime: no

	CBTS_TELEPORT, // agent position changed (teleport)
	// src_agent: relates to agent
	// dst_agent: (float*)&dst_agent is float[3], x/y/z of teleport target
	// overstack_value: undocumented server data
	// is_offcycle: undocumented server data
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_JUMP, // agent jump
	// src_agent: relates to agent
	// dst_agent: 1 if leaving platform, 0 on landing
	// is_offcycle: undocumented server data
	// evtc: limited to agent table outside instances
	// realtime: no

	CBTS_UNKNOWN // unknown/unsupported type newer than this list perhaps
}

/* is friend/foe
enum iff {
	IFF_FRIEND,
	IFF_FOE,
	IFF_UNKNOWN
};

/* combat result */
enum cbtresult {
	CBTR_STRIKE_DAMAGENORMAL, // damage is strike
	CBTR_STRIKE_DAMAGECRIT, // strike was crit
	CBTR_STRIKE_DAMAGEGLANCE, // strike was glance
	CBTR_BLOCK, // blocked eg. mesmer shield 4
	CBTR_EVADE, // evaded, eg. dodge or mesmer sword 2
	CBTR_INTERRUPT, // action was interrupted
	CBTR_ABSORB, // invulnerable or absorbed eg. guardian elite
	CBTR_BLIND, // action missed
	CBTR_KILLINGBLOW, // target was killed by skill
	CBTR_DOWNED, // target was downed by skill
	CBTR_DEFIANCE_DAMAGENORMAL, // damage is to defiance
	CBTR_SKILLCAST, // on-skill-use signal event
	CBTR_CROWDCONTROL, // target was crowdcontrolled
	CBTR_INVERT, // damage was inverted
	CBTR_BUFF_DAMAGECYCLE, // buff damage happened on tick timer
	CBTR_BUFF_DAMAGENOTCYCLE, // buff damage happened outside tick timer
	CBTR_BUFF_DAMAGENOTCYCLEDMGTOTARGETONHIT, // buff damage happened to target on hitting target
	CBTR_BUFF_DAMAGENOTCYCLEDMGTOSOURCEONHIT, // buff damage happened to source on hitting target
	CBTR_BUFF_DAMAGENOTCYCLEDMGTOTARGETONSTACKREMOVE, // buff damage happened to target on buff removal
	CBTR_UNKNOWN
};

/* combat animation */
enum cbtanimation {
	ACTV_NONE,
	ACTV_START_DEFUNC,
	ACTV_QUICKNESS_DEFUNC,
	ACTV_MINIMUM, // stopped animation with reaching minimum of first trigger point or tooltip time
	ACTV_CANCEL, // stopped animation without reaching minimum of first trigger point or tooltip time
	ACTV_RESET, // animation completed fully
	ACTV_NODATA, // same as ACTV_MINIMUM but on 0/uncertain expected duration
	ACTV_UNKNOWN
};

/* combat buff remove type */
enum cbtbuffremove {
	CBTB_NONE, // not used - not this kind of event
	CBTB_ALL, // last/all stacks removed (sent by server)
	CBTB_SINGLE, // single stack removed (sent by server)
	CBTB_MANUAL, // single stack removed (created by arc on all stack remove)
 	CBTB_UNKNOWN
};

/* custom skill ids */
enum n_customskill {
	CSK_DODGE = 23275,
	CSK_DEFIANCEDAMAGE,
	CSK_SELFCAST1,
	CSK_ENEMYCAST1,
	CSK_SELFCAST2,
	CSK_ENEMYCAST2,
	CSK_SELFCAST3,
	CSK_ENEMYCAST3,
	CSK_BREAKBAR_DEFUNC,
	CSK_WEAPONDRAW,
	CSK_WEAPONSTOW,
	CSK_GENERICBLOCK,
	CSK_GENERICDAMAGE,
	CSK_GENERICKILL,
	CSK_GENERICDOWN,
	CSK_GENERICEVADE,
	CSK_GENERICINTERRUPT,
	CSK_GENERICABSORB,
	CSK_GENERICMISS,
	CSK_GENERICKNOCKDOWN,
	CSK_GENERICKNOCKBACKPULL,
	CSK_GENERICFLOATLAND,
	CSK_GENERICLAUNCH,
	CSK_GENERICWATERFLOATSINK_DEFUNC,
	CSK_GENERICCCBUFF,
	CSK_GENERICSTAGGER,
	CSK_GENERICINVALID,
	CSK_GADGETINTERACT,
	CSK_EMOTE, // emote id in animation start
	CSK_GENERICFLOATWATER,
	CSK_GENERICSINK,
	CSK_GENERICLOCKOUT,
	CSK_GENERICFEAR,
	CSK_PICKUP, // item id in animation start
	CSK_GENERICFALLDOWN
};

/* animation start trigger (debug only, subject to change) */
enum n_animationstart {
	ANIMSTART_NONE,
	ANIMSTART_SKILL,
	ANIMSTART_DODGE,
	ANIMSTART_STOWDRAW,
	ANIMSTART_MOVESKILL,
	ANIMSTART_MOTIONSKILL,
	ANIMSTART_GADGETINTERACT,
	ANIMSTART_EMOTE,
	ANIMSTART_PICKUP
};

/* animation stop trigger (debug only, subject to change) */
enum n_animationstop {
	ANIMSTOP_NONE,
	ANIMSTOP_INSTANT,
	ANIMSTOP_MULTI_DEFUNC,
	ANIMSTOP_TRANSITION,
	ANIMSTOP_PARTIAL_DEFUNC,
	ANIMSTOP_ENDED,
	ANIMSTOP_CANCEL,
	ANIMSTOP_STOWDRAW,
	ANIMSTOP_INTERRUPT,
	ANIMSTOP_DEATH,
	ANIMSTOP_DOWNED,
	ANIMSTOP_CROWDCONTROL,
	ANIMSTOP_COMMAND,
	ANIMSTOP_MOTIONSKILL,
	ANIMSTOP_MOVEDODGE,
	ANIMSTOP_MOTIONSKILL_VIA_RESET,
	ANIMSTOP_MOVESKILL,
	ANIMSTOP_STOW,
	ANIMSTOP_ANY_DEFUNC,
	ANIMSTOP_GADGET_VIA_RESET,
	ANIMSTOP_MANUAL_EXPIRY,
	ANIMSTOP_DESPAWN,
	ANIMSTOP_RETURN_CONTROL,
	ANIMSTOP_READY,
	ANIMSTOP_INVISIBLE,
	ANIMSTOP_PICKUP
};

/* language */
enum gwlanguage {
	GWL_ENG = 0,
	GWL_FRE = 2,
	GWL_GEM = 3,
	GWL_SPA = 4,
	GWL_CN = 5,
};

/* content local enum */
enum n_contentlocal {
	CONTENTLOCAL_EFFECT,
	// src_instid: content type
	// buff_dmg: (float*)&buff_dmg is default duration (if available, when effect2 has no duration or agent set)

	CONTENTLOCAL_MARKER,
	// src_instid: is in commandertag defs

	CONTENTLOCAL_SKILL,
	// no extra data, see SKILL/BUFFINFO events

	CONTENTLOCAL_SPECIES_NOT_GADGET,
	// no extra data

	CONTENTLOCAL_EMOTE,
	// no extra data

	CONTENTLOCAL_TRANSFORMATION,
	// no extra data
};

/* evtc agent */
typedef struct evtc_agent {
	uint64_t iid;
	uint32_t prof;
	uint32_t is_elite;
	int16_t toughness;
	int16_t concentration;
	int16_t healing;
	uint16_t hitbox_width;
	int16_t condition;
	uint16_t defunc;
	char name[64];
} evtc_agent;

/* combat event logging (revision 1, when header[12] == 1). all fields except time are event-specific, refer to descriptions of events above */
typedef struct cbtevent {
	uint64_t time; /* timegettime() at time of event */
	uint64_t src_agent;
	uint64_t dst_agent;
	int32_t value;
	int32_t buff_dmg;
	uint32_t overstack_value;
	uint32_t skillid;
	uint16_t src_instid;
	uint16_t dst_instid;
	uint16_t src_master_instid;
	uint16_t dst_master_instid;
	uint8_t iff;
	uint8_t buff;
	uint8_t result;
	uint8_t is_activation;
	uint8_t is_buffremove;
	uint8_t is_ninety;
	uint8_t is_fifty;
	uint8_t is_moving;
	uint8_t is_statechange;
	uint8_t is_flanking;
	uint8_t is_shields;
	uint8_t is_offcycle;
	uint8_t pad61;
	uint8_t pad62;
	uint8_t pad63;
	uint8_t pad64;
} cbtevent;

always included buff skills for CBTS_BUFFFORMULA:
	skill_mask[SKILL_REGENERATION] = 1; // 718
	skill_mask[SKILL_SWIFTNESS] = 1; // 719
	skill_mask[SKILL_POISON] = 1; // 723
	skill_mask[SKILL_FURY] = 1; // 725
	skill_mask[SKILL_VIGOR] = 1; // 726
	skill_mask[SKILL_VULNERABILITY] = 1; // 738
	skill_mask[SKILL_WEAKNESS] = 1; // 742
	skill_mask[SKILL_CONFUSION] = 1; // 861
	skill_mask[SKILL_RESOLUTION] = 1; // 873
	skill_mask[SKILL_TORMENT] = 1; // 19426
	skill_mask[SKILL_MIGHT] = 1; // 740
	skill_mask[SKILL_QUICKNESS] = 1; // 1187
	skill_mask[SKILL_ALACRITY] = 1; // 30328
	skill_mask[SKILL_PROTECTION] = 1; // 717
	skill_mask[SKILL_BLEEDING] = 1; // 736
	skill_mask[SKILL_KALLAS_FERVOR] = 1; // 42883
	skill_mask[9162] = 1;
	skill_mask[9759] = 1;
	skill_mask[9774] = 1;
	skill_mask[9776] = 1;
	skill_mask[9779] = 1;
	skill_mask[9784] = 1;
	skill_mask[9788] = 1;
	skill_mask[9792] = 1;
	skill_mask[10004] = 1;
	skill_mask[10231] = 1;
	skill_mask[10233] = 1;
	skill_mask[12518] = 1;
	skill_mask[13061] = 1;
	skill_mask[14444] = 1;
	skill_mask[14458] = 1;
	skill_mask[14459] = 1;
	skill_mask[15788] = 1;
	skill_mask[15790] = 1;
	skill_mask[17825] = 1;
	skill_mask[25879] = 1;
	skill_mask[29025] = 1;
	skill_mask[29466] = 1;
	skill_mask[36406] = 1;
	skill_mask[43499] = 1;
	skill_mask[44871] = 1;
	skill_mask[46273] = 1;
	skill_mask[51683] = 1;
	skill_mask[53222] = 1;
	skill_mask[62733] = 1;

npcs that get used for starting logs
uint32_t characters[] = {
	/* raids */
	15438, // vale guardian
	15429, // gorseval
	15375, // sabetha
	16123, // slothasor
	16088, // trio - berg
	16115, // matthias
	16253, // mcleod
	16235, // keep construct
	16247, // twisted castle
	16246, // xera 100-50
	17194, // cairn
	17172, // mursaat overseer
	17188, // samarog
	17154, // deimos
	19767, // soulless horror
	19828, // rainbow road
	19691, // statues (ice dude)
	19536, // statues (big dude)
	19844, // statues (eye of fate)
	19450, // dhuum
	21105, // nikare
	20934, // qadim
	21964, // sabir
	22006, // adina
	22000, // qadim2
	26725, // greer
	26774, // decima
	26867, // decima cm
	26712, // ura

	/* special */
	21333, // freezie (wintersday)

	/* fractals */
	17021, // mama
	17028, // siax
	16948, // ensolyss
	17632, // skorvald
	17949, // artsariiv
	17759, // arkk
	23254, // sorrowful spellcaster
	25577, // kanaxai
	26231, // eparch
	27010, // whispering shadow

	/* strike missions */
	22154, // icebrood construct
	22343, // kodan brothers - voice
	22492, // fraenir
	22521, // boneskinner
	22711, // whisper of jormag
	24033, // mai trin
	23957, // ankka
	24485, // minister li
	24266, // minister li cm
	25413, // old lions court - prototype vermillion
	25414, // old lions court cm - prototype vermillion
	25705, // dagda
	25989, // cerus

	/* golems/benchmark */
	16199, // standard kitty golem
	16177,
	16198,
	16178,
	16202,
	16169,
	16176,
	16174,
	19645,
	19676,
};

npcs that get used for vs target dps
uint32_t characters_secondary[] = {
	/* raids */
	// 15420, // vale guardian - green guardian
	// 15431, // vale guardian - blue guardian
	// 15433, // vale guardian - red guardian
	15434, // gorseval - charged soul
	15372, // sabetha - kernan
	15404, // sabetha - knuckles
	15430, // sabetha - karde
	16125, // trio - narella
	16137, // trio - zane
	// 16282, // keep construct - caulle
	// 16274, // keep construct - engul
	// 16264, // keep construct - faerla
	// 16228, // keep construct - galletta
	// 16236, // keep construct - henley
	// 16248, // keep construct - ianim
	// 16278, // keep construct - jessica
	16286, // xera 50-0
	// 17181, // mursaat overseer - jade scout
	// 17124, // samarog - rigom
	17208, // samarog - guldhem
	// 19464, // soulless horror - flesh wurm
	// 19801, // statues (big dude) - twisted spirit
	19651, // statues (eye of judgement)
	// 19681, // dhuum - dhuums enforcer
	21089, // kenut
	// 21285, // qadim - ancient invoked hydra
	// 21073, // qadim - apocalypse bringer
	// 20997, // qadim - wyvern matriarch
	// 21183, // qadim - wyvern patriarch
	// 21955, // sabir - paralyzing wisp cm
	// 21975, // sabir - voltaic wisp
	// 21973, // qadim 2 - entropic distortion
	26771, // gree
	26742, // reeg
	// 26859, // ereg
	26862, // photo-greerling

	/* strike missions */
	22315, // kodan brothers - merged
	22481, // kodan brothers - claw
	22436, // fraenir - construct
	24431, // mai trin - scarlet phantom
	25262, // mai trin - scarlet phantom cm
	24768, // mai trin - scarlet
	25247, // mai trin - scarlet cm
	23612, // minister li - sniper
	24660, // minister li - mechrider
	24261, // minister li - enforcer
	23618, // minister li - ritualist
	24254, // minister li - mindblade
	25259, // minister li cm - sniper
	25271, // minister li cm - mechrider
	25236, // minister li cm - enforcer
	25242, // minister li cm - ritualist
	25280, // minister li cm - mindblade
	25415, // old lions court - prototype arsenite
	25419, // old lions court - prototype indigo
	25416, // old lions court cm - prototype arsenite
	25423, // old lions court cm - prototype indigo

	/* fractals */
	17068, // nightmare - siax
	17578, // observatory - flux anomaly
	17929, // observatory - flux anomaly
	17695, // observatory - flux anomaly
	17651, // observatory - flux anomaly
	17599, // observatory - flux anomaly
	17770, // observatory - flux anomaly
	17851, // observatory - flux anomaly
	17673, // observatory - flux anomaly
	17893, // observatory - archdiviner
	17730, // observatory - gladiator
	26270, // lonely tower - cruelty
	26260, // lonely tower - judgement
};