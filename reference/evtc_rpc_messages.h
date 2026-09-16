#pragma once

#include <cstdint>

// ArcdpsExtension doesn't compile on linux and I can't be bothered to make it do so just to pull in the definition of
// one struct which we barely even need the members for
#ifdef _WIN32
#include <ArcdpsExtension/arcdps_structs_slim.h>
#else
struct cbtevent
{
	uint64_t time;
	uintptr_t src_agent;
	uintptr_t dst_agent;
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
};
#endif

#pragma pack(push, 1)
namespace evtc_rpc
{
namespace messages
{
enum class Type : uint32_t
{
	Invalid = 0,
	RegisterSelf = 1,
	SetSelfId = 2,
	AddPeer = 3,
	RemovePeer = 4,
	CombatEvent = 5,
	Max
};

struct Header
{
	uint32_t MessageVersion;
	Type MessageType;
};
static_assert(sizeof(Header) == 8, "");

struct RegisterSelf
{
	uint16_t SelfId;
	uint8_t SelfAccountNameLength;
	//char SelfAccountName[];
};
static_assert(sizeof(RegisterSelf) == 3, "");

struct SetSelfId
{
	uint16_t SelfId;
};
static_assert(sizeof(SetSelfId) == 2, "");

struct AddPeer
{
	uint16_t PeerId;
	uint8_t PeerAccountNameLength;
	// char PeerAccountName[];
};
static_assert(sizeof(AddPeer) == 3, "");

struct RemovePeer
{
	uint16_t PeerId;
};
static_assert(sizeof(RemovePeer) == 2, "");

/*
struct CombatEvent
{
	cbtevent Event;

	uintptr_t SourceAgentId;
	uintptr_t DestinationAgentId;
	
	Prof SourceAgentProfession;
	Prof DestinationAgentProfession;
	
	uint32_t SourceAgentElite;
	uint32_t DestinationAgentElite;
	
	uint32_t SourceAgentSelf;
	uint32_t DestinationAgentSelf;
	
	uint16_t SourceAgentTeam;
	uint16_t DestinationAgentTeam;

	uint8_t SourceAgentNameLength; // UINT8_MAX => SourceAgentName is nullptr
	uint8_t DestinationAgentNameLength; // UINT8_MAX => DestinationAgentName is nullptr

	// char SourceAgentName[];
	// char DestinationAgentName[];
};
static_assert(sizeof(CombatEvent) == 110, "");
*/

struct CombatEvent
{
	cbtevent Event;
	uint16_t SenderInstanceId; // 0 when sent from client
};
static_assert(sizeof(CombatEvent) == 66, "");

};
};
#pragma pack(pop)