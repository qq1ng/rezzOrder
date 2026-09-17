#pragma once

#include <string>
#include <vector>

#include "Session.h"

// Sharing a revive order through squad chat.
//
// Addons cannot send chat messages, but Unofficial Extras hands every squad message to addons that ask for
// it. So the commander's client writes the order to the clipboard, they paste it into squad chat with one
// keystroke, and every other client reads it from there.
//
// The line is meant to be readable by the people who don't run this addon as well:
//
//   !rezzorder Gorath > Magaton > murako* > Moister > Sairana
//
// Players are named by the part of the account name before the dot, which is what people call each other in
// a squad. The full account name is used when two members of the squad share that part, and names are
// shortened to a unique prefix when the line would not fit in a chat message.
//
// A trailing "*" marks a precast player: somebody who may spend their revive whenever they see a fight
// about to go badly, rather than waiting for their turn. "?rezzorder" asks the squad for the order.
//
// A last entry "bench:5" (or "bench:last") names the squad's bench subgroup, for bench swaps. It is written as one more entry
// so a client that doesn't know it takes it for a name matching nobody and still reads the players; account
// names can't contain a colon, so it can't be mistaken for one.
namespace Rezz::Share
{
	// The game's chat limit. Anything longer would be cut off by the client mid-paste.
	inline constexpr size_t kMaxChatChars = 199;
	inline constexpr const char* kPrefix      = "!rezzorder";
	inline constexpr const char* kRequestText = "?rezzorder";
	inline constexpr char kPrecastMark = '*';
	inline constexpr const char* kBenchToken = "bench:";
	inline constexpr uint16_t kMaxSubgroup = 15;
	// Bench setting for "whichever subgroup is last": squads fill up over an evening and the bench moves with them.
	inline constexpr uint16_t kBenchLast = 255;
	// A bench setting as a chat token or label ("last", "5"), and back. Anything else reads as 0, no bench.
	std::string BenchName(uint16_t aBench);
	uint16_t BenchFromName(const std::string& aName);

	// A squad message is whatever another player typed, so parsing is bounded rather than trusting it to be
	// sensible. Matching each name against the roster costs squad-size work, so an unbounded list of names
	// would let one chat line do a lot of pointless work on every client that reads it. A real order never
	// comes close to these: the game's own chat limit is 199 characters, and a squad holds 50 players.
	inline constexpr size_t kMaxEntries   = 60;  // names read from one line; the rest are ignored
	inline constexpr size_t kMaxNameChars = 64;  // one name; longer is truncated before matching
	inline constexpr size_t kMaxTextChars = 1024; // a whole line, in case the sender's length is wrong

	// The chat line for this order, or empty when the order is empty. aPrecast holds the accounts that may
	// fire early; aBench is the bench subgroup (1-15 or kBenchLast), 0 none.
	std::string Encode(const std::vector<std::string>& aOrder, const std::vector<std::string>& aPrecast,
		const std::vector<RosterMember>& aRoster, uint16_t aBench = 0);

	enum class Kind : uint8_t { None, Order, Request };

	struct Entry
	{
		std::string Name;             // as written in the message
		bool        Precast = false;  // it carried the "*"
	};

	struct Message
	{
		Kind               What = Kind::None;
		std::vector<Entry> Entries; // in the order they were named
		uint16_t           Bench = 0; // bench subgroup (1-15 or kBenchLast), 0 none given
	};

	// Reads a chat line. Anything that isn't one of ours comes back as Kind::None.
	Message Parse(const std::string& aText);

	struct Resolved
	{
		std::vector<std::string> Accounts; // squad members, in the order they were named
		std::vector<std::string> Precast;  // those of them marked as free to fire early
		std::vector<std::string> Unknown;  // names that match nobody, or more than one player
	};

	// Matches the names against the squad: account name, its first part, a unique prefix of either, or the
	// character name.
	Resolved Resolve(const std::vector<Entry>& aEntries, const std::vector<RosterMember>& aRoster);
}
