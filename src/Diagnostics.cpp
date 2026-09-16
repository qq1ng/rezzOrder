#include "Diagnostics.h"

#include <algorithm>
#include <cwctype>
#include <iterator>

#include <Windows.h>
#include <psapi.h>

namespace Diagnostics
{
	Dependencies Detect()
	{
		Dependencies deps;
		deps.Checked = true;

		HMODULE modules[1024];
		DWORD needed = 0;
		if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) { return deps; }

		size_t count = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));
		for (size_t i = 0; i < count; i++)
		{
			// arcdps may be loaded as ArcDPS.dll, d3d11.dll or a chainload name; its exports identify it.
			if (GetProcAddress(modules[i], "addextension2") != nullptr) { deps.ArcDps = true; }

			wchar_t path[MAX_PATH];
			if (GetModuleFileNameW(modules[i], path, MAX_PATH) == 0) { continue; }
			std::wstring name(path);
			name = name.substr(name.find_last_of(L"\\/") + 1);
			for (wchar_t& c : name) { c = static_cast<wchar_t>(std::towlower(c)); }

			if (name.find(L"arcdps_integration") != std::wstring::npos) { deps.Integration = true; }
			if (name.find(L"unofficial_extras") != std::wstring::npos) { deps.UnofficialExtras = true; }
		}
		return deps;
	}

	std::string Problems(const Dependencies& aDeps)
	{
		std::string problems;
		auto add = [&](const char* aText)
		{
			if (!problems.empty()) { problems += " | "; }
			problems += aText;
		};
		if (!aDeps.ArcDps)           { add("ArcDPS not found (install it from the Nexus library)"); }
		if (!aDeps.Integration)      { add("Arcdps Integration not loaded (restart the game after installing ArcDPS)"); }
		if (!aDeps.UnofficialExtras) { add("Unofficial Extras not found (install it from the Nexus library)"); }
		return problems;
	}
}
