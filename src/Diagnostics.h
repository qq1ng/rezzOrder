#pragma once

#include <string>

// Checks which data sources the recorder depends on are loaded in the game process.
namespace Diagnostics
{
	struct Dependencies
	{
		bool Checked          = false;
		bool ArcDps           = false; // any module exporting arcdps' extension API
		bool Integration      = false; // Arcdps Integration (relays arcdps + Unofficial Extras to Nexus)
		bool UnofficialExtras = false; // squad roster and chat
	};

	Dependencies Detect();

	// Short human-readable problem list, empty when everything is present.
	std::string Problems(const Dependencies& aDeps);
}
