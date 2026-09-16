#include <atomic>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include <Windows.h>

#include "imgui/imgui.h"
#include "mumble/Mumble.h"
#include "nexus/Nexus.h"

#include "Arc.h"
#include "Capture.h"
#include "Demo.h"
#include "Diagnostics.h"
#include "Fonts.h"
#include "Icons.h"
#include "Live.h"
#include "Notify.h"
#include "OrderUi.h"
#include "Settings.h"
#include "Ui.h"
#include "unofficial_extras/Definitions.h"

#include <mmsystem.h>

#define ADDON_NAME "Rezz Order"
#define ADDON_VERSION_STRING "0.2.1"

namespace
{
	constexpr const char* KB_TOGGLE = "KB_REZZORDER_TOGGLE";
	constexpr const char* KB_MARK   = "KB_REZZORDER_MARK";
	constexpr const char* KB_EDITOR = "KB_REZZORDER_EDITOR";
	constexpr const char* KB_LOCK   = "KB_REZZORDER_LOCK_OVERLAY";
	constexpr const char* KB_COPY   = "KB_REZZORDER_COPY_ORDER";
	constexpr const char* QA_MENU_ITEM = "QA_REZZORDER_MENU";

	// Events raised by the Arcdps Integration addon (arcdps + Unofficial Extras relay).
	constexpr const char* EV_COMBAT_SQUAD = "EV_ARCDPS_COMBATEVENT_SQUAD_RAW";
	constexpr const char* EV_COMBAT_LOCAL = "EV_ARCDPS_COMBATEVENT_LOCAL_RAW";
	constexpr const char* EV_SELF_JOIN    = "EV_ARCDPS_SELF_JOIN";
	constexpr const char* EV_SELF_LEAVE   = "EV_ARCDPS_SELF_LEAVE";
	constexpr const char* EV_SQUAD_JOIN   = "EV_ARCDPS_SQUAD_JOIN";
	constexpr const char* EV_SQUAD_LEAVE  = "EV_ARCDPS_SQUAD_LEAVE";
	constexpr const char* EV_UE_SQUAD     = "EV_UNOFFICIAL_EXTRAS_SQUAD_UPDATE";
	constexpr const char* EV_UE_CHAT      = "EV_UNOFFICIAL_EXTRAS_CHAT_MESSAGE";

	// Payload of EV_UNOFFICIAL_EXTRAS_SQUAD_UPDATE as raised by Arcdps Integration.
	struct SquadUpdate
	{
		UserInfo* Users;
		uint64_t  Count;
	};

	// Dependency check timing: modules loaded after us need a moment; arcdps data needs time on a WvW map.
	constexpr uint32_t kDependencyCheckAfterMs = 20 * 1000;
	constexpr uint32_t kNoDataAlertAfterMs     = 90 * 1000;

	AddonDefinition_t s_AddonDef{};
	AddonAPI_t*       s_Api       = nullptr;
	NexusLinkData_t*  s_NexusLink = nullptr;
	Mumble::Data*     s_Mumble    = nullptr;
	uint32_t          s_LastMapId = 0;
	uint32_t          s_LoadMs    = 0;
	uint32_t          s_WvwSinceMs = 0; // render thread: when we last entered a WvW map (0 = not in WvW)
	bool              s_NoDataAlerted = false;

	// Everything below is called from Nexus, from arcdps or from the game's window procedure. All three are C
	// boundaries, so an exception that escapes one ends the process: the player loses the game, not just this
	// addon. Nothing here is worth that, so every callback catches.
	//
	// The same guard keeps the addon alive for the length of a call. Nexus can unload an addon at runtime (an
	// update does exactly that) and arcdps states its combat callback "may be called asynchronously", so a
	// callback can still be running while Unload tears its state down. Counting calls in flight lets Unload
	// wait for them instead.
	std::atomic<bool> s_Alive{false};
	std::atomic<int>  s_InFlight{0};
	std::atomic<int>  s_Failures{0};

	void LogFailure(const char* aWhat, const char* aDetail)
	{
		// One line per callback kind is enough to find the cause; a throw that repeats every frame must not
		// turn into its own problem.
		if (s_Failures.fetch_add(1) >= 20 || s_Api == nullptr) { return; }
		std::string message = std::string("Caught an exception in ") + aWhat + ": " + aDetail;
		s_Api->Log(LOGL_WARNING, ADDON_NAME, message.c_str());
	}

	template <typename F>
	void Guarded(const char* aWhat, F&& aFunction)
	{
		// Counted before the check, so Unload either waits for this call or sees the addon already closed.
		s_InFlight.fetch_add(1);
		if (s_Alive.load())
		{
			try                             { aFunction(); }
			catch (const std::exception& e) { LogFailure(aWhat, e.what()); }
			catch (...)                     { LogFailure(aWhat, "unknown exception"); }
		}
		s_InFlight.fetch_sub(1);
	}

	void StartRecording();

	void OnCombatSquad(void* aArgs)
	{
		Guarded("the squad combat event", [aArgs]
		{
			auto* data = static_cast<ArcDps::EvCombatData*>(aArgs);
			Live::OnCombatSquad(data);
			Capture::OnCombat(Capture::CH_SQUAD, data);
		});
	}

	void OnCombatLocal(void* aArgs)
	{
		Guarded("the local combat event", [aArgs]
		{
			Capture::OnCombat(Capture::CH_LOCAL, static_cast<ArcDps::EvCombatData*>(aArgs));
		});
	}

	void OnAgent(const char* aName, void* aArgs)
	{
		Guarded(aName, [aName, aArgs]
		{
			auto* update = static_cast<ArcDps::EvAgentUpdate*>(aArgs);
			Live::OnAgentUpdate(update);
			Capture::OnAgentUpdate(aName, update);
		});
	}

	void OnSelfJoin(void* aArgs)    { OnAgent("SELF_JOIN", aArgs); }
	void OnSelfLeave(void* aArgs)
	{
		// Swapping character or hopping maps: the fake squad would outlive what it was started for.
		Guarded("SELF_LEAVE", [] { Rezz::Demo::Stop(); });
		OnAgent("SELF_LEAVE", aArgs);
	}
	void OnSquadJoin(void* aArgs)   { OnAgent("SQUAD_JOIN", aArgs); }
	void OnSquadLeave(void* aArgs)  { OnAgent("SQUAD_LEAVE", aArgs); }

	void OnUeSquadUpdate(void* aArgs)
	{
		Guarded("the squad update", [aArgs]
		{
			if (auto* update = static_cast<SquadUpdate*>(aArgs))
			{
				Live::OnSquadUpdate(update->Users, update->Count);
				Capture::OnSquadUpdate(update->Users, update->Count);
			}
		});
	}

	void OnUeChatMessage(void* aArgs)
	{
		Guarded("the chat message", [aArgs]
		{
			auto* message = static_cast<SquadMessageInfo*>(aArgs);
			Live::OnChatMessage(message);
			Capture::OnChatMessage(message);
		});
	}

	UINT OnWndProc(HWND, UINT aMsg, WPARAM aWParam, LPARAM aLParam)
	{
		Guarded("the window procedure", [aMsg, aWParam, aLParam]
		{
			if (aMsg == WM_KEYDOWN || aMsg == WM_SYSKEYDOWN || aMsg == WM_XBUTTONDOWN || aMsg == WM_MBUTTONDOWN)
			{
				bool typing = Ui::TextInputActive || OrderUi::TextInputActive ||
					(s_Mumble && s_Mumble->Context.IsTextboxFocused);
				Capture::OnKey(aMsg, aWParam, aLParam, typing);
			}
		});
		return aMsg; // never consume input, whatever happened above
	}

	void OnInputBind(const char* aIdentifier, bool aIsRelease)
	{
		Guarded("the input bind", [aIdentifier, aIsRelease]
		{
			if (aIsRelease) { return; }
			if (std::strcmp(aIdentifier, KB_TOGGLE) == 0) { Ui::ToggleRequested = true; }
			else if (std::strcmp(aIdentifier, KB_MARK) == 0) { Ui::MarkRequested = true; }
			else if (std::strcmp(aIdentifier, KB_EDITOR) == 0) { OrderUi::EditorToggleRequested = true; }
			else if (std::strcmp(aIdentifier, KB_LOCK) == 0) { OrderUi::LockToggleRequested = true; }
			else if (std::strcmp(aIdentifier, KB_COPY) == 0) { OrderUi::CopyOrderRequested = true; }
		});
	}

	// Every message shows in the turn window; these are the ones worth putting across the screen as well.
	bool WantsBanner(Rezz::NoticeKind aKind)
	{
		const Settings::Values& s = Settings::Current;
		switch (aKind)
		{
			case Rezz::NoticeKind::LeftSquad:
			case Rezz::NoticeKind::LeftMap:           return s.BannerOnLeave;
			case Rezz::NoticeKind::ChangedProfession: return s.BannerOnSwap;
			case Rezz::NoticeKind::ShareApplied:
			case Rezz::NoticeKind::ShareOffered:      return s.BannerOnShare;
			case Rezz::NoticeKind::ShareRequested:    return s.BannerOnAsk;
			// Somebody coming back, and our own order being cleared when we leave the squad, are shown in
			// the window only: neither needs the screen.
			default: return false;
		}
	}

	void OnStandingBanner(const std::string& aText)
	{
		if (s_Api) { s_Api->GUI_SendAlert(("Rezz Order: " + aText).c_str()); }
	}

	void CheckDependencies(uint32_t aNowMs)
	{
		if (!Ui::Deps.Checked && aNowMs - s_LoadMs >= kDependencyCheckAfterMs)
		{
			Ui::Deps = Diagnostics::Detect();
			std::string problems = Diagnostics::Problems(Ui::Deps);
			Capture::LogInfo("dependencies arcdps=" + std::to_string(Ui::Deps.ArcDps) +
				" integration=" + std::to_string(Ui::Deps.Integration) +
				" unofficial_extras=" + std::to_string(Ui::Deps.UnofficialExtras));
			if (problems.empty())
			{
				// Only on the first run of a build: after that the player knows it works.
				if (Settings::Current.LastSeenVersion != ADDON_VERSION_STRING)
				{
					s_Api->GUI_SendAlert("Rezz Order " ADDON_VERSION_STRING ": all set.");
					Settings::Current.LastSeenVersion = ADDON_VERSION_STRING;
					Settings::MarkDirty();
				}
			}
			else
			{
				s_Api->GUI_SendAlert(("Rezz Order problem: " + problems).c_str());
			}
		}

		// Everything loaded but no arcdps events after a while on a WvW map: the data isn't reaching us.
		bool inWvw = Capture::GetStatus().InWvw;
		if (!inWvw) { s_WvwSinceMs = 0; return; }
		if (s_WvwSinceMs == 0) { s_WvwSinceMs = aNowMs; }
		if (!s_NoDataAlerted && aNowMs - s_WvwSinceMs >= kNoDataAlertAfterMs && Capture::GetStatus().ArcEvents == 0)
		{
			s_NoDataAlerted = true;
			Capture::LogInfo("no arcdps events after 90 s on a WvW map");
			s_Api->GUI_SendAlert("Rezz Order problem: no combat data from ArcDPS yet. Is ArcDPS enabled?");
		}
	}

	void RenderFrame()
	{
		uint32_t now = timeGetTime();
		if (s_Mumble && s_Mumble->Context.MapID != s_LastMapId)
		{
			s_LastMapId = s_Mumble->Context.MapID;
			Capture::OnMapChange(s_LastMapId, static_cast<uint8_t>(s_Mumble->Context.MapType), s_Mumble->Context.IsCompetitive);
			// A demo is for setting the window up where you are standing; a new map is a new situation.
			Rezz::Demo::Stop();
		}

		Capture::TickInfo tick;
		tick.NowMs = now;
		if (s_Mumble && (s_NexusLink == nullptr || s_NexusLink->IsGameplay))
		{
			tick.HasPosition = true;
			tick.Position[0] = s_Mumble->AvatarPosition.X;
			tick.Position[1] = s_Mumble->AvatarPosition.Y;
			tick.Position[2] = s_Mumble->AvatarPosition.Z;
		}
		Capture::Tick(tick);
		CheckDependencies(now);

		Live::Tick();
		for (const Rezz::Notice& notice : Live::TakeNotices())
		{
			OrderUi::AddNotice(notice.Text, now);
			if (notice.Kind == Rezz::NoticeKind::OrderCleared) { OrderUi::OnOrderCleared(); }
			if (WantsBanner(notice.Kind)) { s_Api->GUI_SendAlert(("Rezz Order: " + notice.Text).c_str()); }
		}

		bool gameplay = s_NexusLink == nullptr || s_NexusLink->IsGameplay;
		OrderUi::Context context;
		context.NowMs = now;
		context.IsGameplay = gameplay;
		context.IsMapOpen = s_Mumble && s_Mumble->Context.IsMapOpen;
		context.InWvw = Capture::GetStatus().InWvw;
		OrderUi::Render(context);
		Ui::Render(gameplay);
	}

	// If a draw ever throws part way through a window, ImGui's stack is left unbalanced, which is bad. It is
	// still better than letting the exception reach Nexus, which would take the game down with it.
	void OnRender() { Guarded("the render callback", [] { RenderFrame(); }); }

	void OnOptions()
	{
		Guarded("the options page", []
		{
		OrderUi::Options();
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Field recorder (testing)"))
		{
			bool record = Settings::Current.RecordFieldLogs;
			if (ImGui::Checkbox("Record a field log while in WvW", &record))
			{
				Settings::Current.RecordFieldLogs = record;
				Settings::MarkDirty();
				if (record) { StartRecording(); } else { Capture::Stop(); }
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", "Writes a CSV of combat events to the addon's logs folder, for working "
					"out why tracking went wrong. It includes the account and character names of everyone in "
					"your squad, so leave it off unless you mean to send one in.");
			}
			Ui::Options();
		}
		});
	}

	void OnQuickAccessMenu()
	{
		Guarded("the quick access menu", []
		{
			if (ImGui::Button("Rezz Order editor")) { OrderUi::ShowEditor = true; }
			if (ImGui::Button("Copy order for squad chat")) { OrderUi::CopyOrderRequested = true; }
		});
	}

	// Split out so the options page can start and stop recording without a reload.
	void StartRecording()
	{
		std::filesystem::path addonDir = s_Api->Paths_GetAddonDirectory("RezzOrder");
		if (!Capture::Start(addonDir / "logs", ADDON_VERSION_STRING))
		{
			s_Api->Log(LOGL_WARNING, ADDON_NAME, "Could not create the log file; nothing will be recorded.");
		}
	}

	void AddonLoad(AddonAPI_t* aApi)
	{
		s_Api = aApi;
		ImGui::SetCurrentContext(static_cast<ImGuiContext*>(s_Api->ImguiContext));
		ImGui::SetAllocatorFunctions(
			static_cast<void* (*)(size_t, void*)>(s_Api->ImguiMalloc),
			static_cast<void (*)(void*, void*)>(s_Api->ImguiFree));

		s_NexusLink = static_cast<NexusLinkData_t*>(s_Api->DataLink_Get("DL_NEXUS_LINK"));
		s_Mumble = static_cast<Mumble::Data*>(s_Api->DataLink_Get("DL_MUMBLE_LINK"));

		s_LoadMs = timeGetTime();
		std::filesystem::path addonDir = s_Api->Paths_GetAddonDirectory("RezzOrder");
		Settings::Load(addonDir / "settings.txt");
		Icons::Init(s_Api);
		Fonts::Init(s_Api, s_NexusLink);
		OrderUi::Init();

		// The field recorder exists to gather test data, and what it writes includes the account and character
		// names of everyone in the squad. That is not something to do on a player's machine unless they asked
		// for it, so it stays off until somebody turns it on in the options.
		if (Settings::Current.RecordFieldLogs) { StartRecording(); }

		// Live from here: callbacks may arrive on other threads the moment they are subscribed.
		s_Alive = true;

		s_Api->Events_Subscribe(EV_COMBAT_SQUAD, OnCombatSquad);
		s_Api->Events_Subscribe(EV_COMBAT_LOCAL, OnCombatLocal);
		s_Api->Events_Subscribe(EV_SELF_JOIN, OnSelfJoin);
		s_Api->Events_Subscribe(EV_SELF_LEAVE, OnSelfLeave);
		s_Api->Events_Subscribe(EV_SQUAD_JOIN, OnSquadJoin);
		s_Api->Events_Subscribe(EV_SQUAD_LEAVE, OnSquadLeave);
		s_Api->Events_Subscribe(EV_UE_SQUAD, OnUeSquadUpdate);
		s_Api->Events_Subscribe(EV_UE_CHAT, OnUeChatMessage);

		// Ask Arcdps Integration to resend self/squad agents we missed when loaded mid-session.
		s_Api->Events_RaiseNotification("EV_REPLAY_ARCDPS_SELF_JOIN");
		s_Api->Events_RaiseNotification("EV_REPLAY_ARCDPS_SQUAD_JOIN");

		s_Api->WndProc_Register(OnWndProc);
		s_Api->InputBinds_RegisterWithString(KB_TOGGLE, OnInputBind, "CTRL+SHIFT+R");
		s_Api->InputBinds_RegisterWithString(KB_MARK, OnInputBind, "CTRL+SHIFT+M");
		s_Api->InputBinds_RegisterWithString(KB_EDITOR, OnInputBind, "CTRL+SHIFT+O");
		s_Api->InputBinds_RegisterWithString(KB_LOCK, OnInputBind, "CTRL+SHIFT+L");
		s_Api->InputBinds_RegisterWithString(KB_COPY, OnInputBind, "CTRL+SHIFT+K");
		s_Api->GUI_Register(RT_Render, OnRender);
		s_Api->GUI_Register(RT_OptionsRender, OnOptions);
		s_Api->GUI_RegisterCloseOnEscape(Ui::kWindowName, &Ui::ShowWindow);
		s_Api->GUI_RegisterCloseOnEscape(OrderUi::kEditorName, &OrderUi::ShowEditor);
		s_Api->QuickAccess_AddContextMenu(QA_MENU_ITEM, "QA_MENU", OnQuickAccessMenu);

		Notify::Init();
		OrderUi::StandingBanner = OnStandingBanner;
		s_Api->Log(LOGL_INFO, ADDON_NAME, "Loaded.");
	}

	void AddonUnload()
	{
		// Stop accepting work first, then wait for calls already inside a callback. Without this, a combat
		// event arriving on another thread could still be walking state that the lines below are destroying.
		s_Alive = false;
		for (int spins = 0; s_InFlight.load() > 0 && spins < 1000; spins++) { ::Sleep(1); }

		s_Api->QuickAccess_RemoveContextMenu(QA_MENU_ITEM);
		s_Api->GUI_DeregisterCloseOnEscape(OrderUi::kEditorName);
		s_Api->GUI_DeregisterCloseOnEscape(Ui::kWindowName);
		s_Api->GUI_Deregister(OnOptions);
		s_Api->GUI_Deregister(OnRender);
		s_Api->InputBinds_Deregister(KB_COPY);
		s_Api->InputBinds_Deregister(KB_LOCK);
		s_Api->InputBinds_Deregister(KB_EDITOR);
		s_Api->InputBinds_Deregister(KB_MARK);
		s_Api->InputBinds_Deregister(KB_TOGGLE);
		OrderUi::StandingBanner = nullptr;
		Notify::Shutdown();
		s_Api->WndProc_Deregister(OnWndProc);

		s_Api->Events_Unsubscribe(EV_UE_CHAT, OnUeChatMessage);
		s_Api->Events_Unsubscribe(EV_UE_SQUAD, OnUeSquadUpdate);
		s_Api->Events_Unsubscribe(EV_SQUAD_LEAVE, OnSquadLeave);
		s_Api->Events_Unsubscribe(EV_SQUAD_JOIN, OnSquadJoin);
		s_Api->Events_Unsubscribe(EV_SELF_LEAVE, OnSelfLeave);
		s_Api->Events_Unsubscribe(EV_SELF_JOIN, OnSelfJoin);
		s_Api->Events_Unsubscribe(EV_COMBAT_LOCAL, OnCombatLocal);
		s_Api->Events_Unsubscribe(EV_COMBAT_SQUAD, OnCombatSquad);

		Capture::Stop();
		Settings::Flush(timeGetTime(), true);
		Icons::Shutdown();
		Fonts::Shutdown();

		s_Mumble = nullptr;
		s_NexusLink = nullptr;
		s_Api = nullptr;
	}
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
	return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
	s_AddonDef.Signature = static_cast<uint32_t>(-48213907); // unique negative id for addons not hosted on Raidcore
	s_AddonDef.APIVersion = NEXUS_API_VERSION;
	s_AddonDef.Name = ADDON_NAME;
	s_AddonDef.Version.Major = REZZ_VERSION_MAJOR;
	s_AddonDef.Version.Minor = REZZ_VERSION_MINOR;
	s_AddonDef.Version.Build = REZZ_VERSION_PATCH;
	s_AddonDef.Version.Revision = 0;
	s_AddonDef.Author = "qq1ng";
	s_AddonDef.Description = "Tracks the squad's instant revive skills and whose turn is next.";
	s_AddonDef.Load = AddonLoad;
	s_AddonDef.Unload = AddonUnload;
	s_AddonDef.Flags = AF_None;
	s_AddonDef.Provider = UP_None;
	return &s_AddonDef;
}
