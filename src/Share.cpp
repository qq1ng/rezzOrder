#include "Share.h"

#include <algorithm>
#include <cctype>

namespace Rezz::Share
{
	namespace
	{
		constexpr const char* kSeparator = " > ";
		// Shorter than this a name stops being recognisable, so the line drops players instead.
		constexpr size_t kMinPrefix = 3;

		std::string Lower(std::string aText)
		{
			std::transform(aText.begin(), aText.end(), aText.begin(),
				[](unsigned char aChar) { return static_cast<char>(std::tolower(aChar)); });
			return aText;
		}

		std::string Trim(const std::string& aText)
		{
			size_t first = aText.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) { return {}; }
			size_t last = aText.find_last_not_of(" \t\r\n");
			return aText.substr(first, last - first + 1);
		}

		// ":Gorath.5076" -> "Gorath.5076"
		std::string WithoutColon(const std::string& aAccount)
		{
			return !aAccount.empty() && aAccount[0] == ':' ? aAccount.substr(1) : aAccount;
		}

		bool StartsWith(const std::string& aText, const std::string& aPrefix)
		{
			return aText.size() >= aPrefix.size() && aText.compare(0, aPrefix.size(), aPrefix) == 0;
		}

		std::string Join(const std::vector<std::string>& aTokens, const std::vector<bool>& aPrecast)
		{
			std::string line = kPrefix;
			for (size_t i = 0; i < aTokens.size(); i++)
			{
				line += (i == 0 ? " " : kSeparator) + aTokens[i];
				if (i < aPrecast.size() && aPrecast[i]) { line += kPrecastMark; }
			}
			return line;
		}
	}

	std::string Encode(const std::vector<std::string>& aOrder, const std::vector<std::string>& aPrecast,
		const std::vector<RosterMember>& aRoster)
	{
		if (aOrder.empty()) { return {}; }

		// The part before the dot is what people call each other, unless two squad members share it.
		std::vector<std::string> tokens;
		std::vector<bool> precast;
		for (const std::string& account : aOrder)
		{
			std::string display = DisplayAccount(account);
			int sharing = 0;
			for (const RosterMember& member : aRoster)
			{
				if (Lower(DisplayAccount(member.Account)) == Lower(display)) { sharing++; }
			}
			tokens.push_back(sharing > 1 ? WithoutColon(account) : display);
			precast.push_back(std::find(aPrecast.begin(), aPrecast.end(), account) != aPrecast.end());
		}

		std::string line = Join(tokens, precast);
		if (line.size() <= kMaxChatChars) { return line; }

		// Too long: shorten every name, longest prefix first, and stop at the first length that fits. Lengths
		// where two names would become the same are skipped, so the other clients can still tell who is who.
		for (size_t length = 8; length >= kMinPrefix; length--)
		{
			std::vector<std::string> shortened;
			for (const std::string& token : tokens) { shortened.push_back(token.substr(0, std::min(length, token.size()))); }
			std::vector<std::string> unique = shortened;
			std::sort(unique.begin(), unique.end());
			if (std::adjacent_find(unique.begin(), unique.end()) != unique.end()) { continue; }

			line = Join(shortened, precast);
			tokens = shortened; // the shortest that still tells them apart, in case nothing fits
			if (line.size() <= kMaxChatChars) { return line; }
		}

		// Still too long: the players at the end of a very long order are the ones who lose their turn last.
		while (tokens.size() > 1 && Join(tokens, precast).size() > kMaxChatChars)
		{
			tokens.pop_back();
			precast.pop_back();
		}
		return Join(tokens, precast);
	}

	Message Parse(const std::string& aText)
	{
		Message message;
		std::string text = Trim(aText);
		std::string lower = Lower(text);

		// "?rezzorder" asks; "!rezzorder ..." tells.
		if (StartsWith(lower, kRequestText)) { message.What = Kind::Request; return message; }
		if (!StartsWith(lower, kPrefix)) { return message; }

		std::string rest = Trim(text.substr(std::string(kPrefix).size()));
		if (rest.empty() || rest == "?") { message.What = Kind::Request; return message; }

		// Both separators are accepted: people type what they are used to.
		std::string token;
		for (char character : rest + ">")
		{
			if (character == '>' || character == ',')
			{
				std::string name = Trim(token);
				token.clear();
				if (name.empty()) { continue; }

				Entry entry;
				// A trailing "*" means they may spend their revive before their turn comes.
				if (name.back() == kPrecastMark)
				{
					entry.Precast = true;
					name.pop_back();
					name = Trim(name);
				}
				if (name.empty()) { continue; }
				entry.Name = name;
				message.Entries.push_back(entry);
				continue;
			}
			token += character;
		}
		if (!message.Entries.empty()) { message.What = Kind::Order; }
		return message;
	}

	Resolved Resolve(const std::vector<Entry>& aEntries, const std::vector<RosterMember>& aRoster)
	{
		Resolved resolved;
		for (const Entry& entry : aEntries)
		{
			std::string wanted = Lower(Trim(entry.Name));
			if (wanted.empty()) { continue; }

			std::vector<std::string> exact;
			std::vector<std::string> prefix;
			for (const RosterMember& member : aRoster)
			{
				std::string account = Lower(WithoutColon(member.Account));
				std::string display = Lower(DisplayAccount(member.Account));
				std::string character = Lower(member.Character);
				if (wanted == account || wanted == display || (!character.empty() && wanted == character))
				{
					exact.push_back(member.Account);
				}
				else if (StartsWith(account, wanted) || StartsWith(display, wanted) ||
					(!character.empty() && StartsWith(character, wanted)))
				{
					prefix.push_back(member.Account);
				}
			}

			const std::vector<std::string>& matches = !exact.empty() ? exact : prefix;
			bool already = !matches.empty() &&
				std::find(resolved.Accounts.begin(), resolved.Accounts.end(), matches.front()) != resolved.Accounts.end();
			if (matches.size() != 1 || already) { resolved.Unknown.push_back(entry.Name); continue; }

			resolved.Accounts.push_back(matches.front());
			if (entry.Precast) { resolved.Precast.push_back(matches.front()); }
		}
		return resolved;
	}
}
