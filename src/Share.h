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
//   !rezz Gorath > Magaton > murako > Moister > Sairana
//
// Players are named by the part of the account name before the dot, which is what people call each other in
// a squad. The full account name is used when two members of the squad share that part, and names are
// shortened to a unique prefix when the line would not fit in a chat message.
namespace Rezz::Share
{
	// The game's chat limit. Anything longer would be cut off by the client mid-paste.
	inline constexpr size_t kMaxChatChars = 199;
	inline constexpr const char* kPrefix      = "!rezz";
	inline constexpr const char* kRequestText = "!rezz?";

	// The chat line for this order, or empty when the order is empty.
	std::string Encode(const std::vector<std::string>& aOrder, const std::vector<RosterMember>& aRoster);

	enum class Kind : uint8_t { None, Order, Request };

	struct Message
	{
		Kind                     What = Kind::None;
		std::vector<std::string> Names; // as written in the message, in order
	};

	// Reads a chat line. Anything that isn't one of ours comes back as Kind::None.
	Message Parse(const std::string& aText);

	struct Resolved
	{
		std::vector<std::string> Accounts; // squad members, in the order they were named
		std::vector<std::string> Unknown;  // names that match nobody, or more than one player
	};

	// Matches the names against the squad: account name, its first part, a unique prefix of either, or the
	// character name.
	Resolved Resolve(const std::vector<std::string>& aNames, const std::vector<RosterMember>& aRoster);
}
