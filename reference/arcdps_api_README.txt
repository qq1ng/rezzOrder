usage:
currently built with imgui 1.92.7 release as of may7 2026 (https://github.com/ocornut/imgui/archive/refs/tags/v1.92.7.zip)
https://help.guildwars2.com/hc/en-us/articles/360013625034-Policy-Third-Party-Programs - read this
https://www.reddit.com/r/Guildwars2/comments/66m13h/anet_this_really_should_be_part_of_the_game/dgjwapj/ - read thisarcdps expects the module to have both get_init_addr and get_release_addr exported.
exported function names must be c-style exports (unmangled).
place arcdps_some_module.dll in gw2 root dir or in bin64 (if your dll name does not include arcdps, it will not be logged).

---

required extension module exports:

void*       get_init_addr(char* arcversionstr,
                          void* imguicontext,
                          void* id3dptr,
                          HANDLE arcdll,
                          void* mallocfn,
                          void* freefn,
                          uint32_t imguiversion)
      arcversionstr: arcdps build date string (eg. 20221023.100940-439-x64)
      imguicontext: for ImGui::SetCurrentContext
      id3dptr: active swapchain
      arcdll: handle to arcdps dll
      malllocfn: for ImGui::SetAllocatorFunctions
      freefn: for ImGui::SetAllocatorFunctions
      imguiversion: IMGUI_VERSION_NUMBER (previously directx version)

      expected to return a pointer to a function that will be called for module initialization.
      module initialization function has no parameters and returns a pointer to an arcdps exports table -- arcdps_exports* fn();

void*       get_release_addr(uint32_t reason)
      reason: of enum n_arcdpsextensionload below, reason for unload

      expected to return a pointer to a function that will be called for module removal.
      module removal function has no parameters and no return value -- void fn();
      returning 0 will trigger a warning without removing the function table or unloading the module.

---

optional extension module exports:

const wchar_t*     get_update_url()
      no parameters

      expected for the extension to check for an update, and to return null, or a wchar_t* string, eg. www.mydomain.com/myfile.dll (443/HTTPS only).
      if a string is returned the module is freelibrary'd, replaced with the file from url, and re-loaded.

---

returned extension function table:

    typedef struct arcdps_exports {
    	uint64_t size; // [required]
    	uint32_t sig; // [required]
    	uint32_t imguivers; // [required]
    	const char* out_name; // [required]
    	const char* out_build; // [required]
    	void* wnd_nofilter;
    	void* combat;
    	void* imgui;
    	void* options_tab;
    	void* combat_local;
    	void* wnd_filter;
    	void* options_windows;
    } arcdps_exports;

    typedef struct ag {
      const char* name;
      uintptr_t id;
      uint32_t prof;
      uint32_t elite;
      uint32_t self;
      uint16_t team;
    } ag;

    size: = sizeof(arcdps_exports)
    sig: pick a number between 1 and uint32_max that is not used by any other module, this is how arc identifies duplicate loads. to signal  an error in loading, set sig to 0, and set size to a cast of a char* error message (or set to 0)
    imguivers: = IMGUI_VERSION_NUM
    out_name: = "your module name", name string shown in options
    out_build: = "1.2.40", version string
    wnd_nofilter: UINT fn(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
            return value is assigned to real uMsg.
            unfiltered window event messages.
    combat: void fn(cbtevent* ev, ag* src, ag* dst, const char* skillname, uint64_t id, uint64_t revision)
            called asynchronously.
            ev parameter is cbtevent as in evtc documentation.
            ev may be null. if so, if src->elite == 1, then src->id is the id of the new targeted agent.
                                    else, if src->prof, src->id was added
                                                              src->name = char name
                                                              dst->name = acc names
                                                              src->id = id
                                                              dst->id = instance id on map
                                                              dst->prof = prof
                                                              dst->elite = elite spec
                                                              dst->self = is self
                                                              src->team = team id
                                                              dst->team = subgroup
                                          else, src->id was removed
            use id parameter to re-establish order of events (if id == 0, consider the event unordered).
            due to unforseen circumstances after a change in assigning id, the first event will always have id == 2.
            refer to ag typedef for src and dst parameters.
            revision is cbtevent type revision, will most likely be 1.
            events are delayed by ~2-3 seconds - this is intended for statistics, not realtime notifications.
    imgui: void fn(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc)
            called before ImGui::Render.
            not_charsel_or_loading and hide_if_combat_or_ooc may be used to match arcdps window visibility.
    options_tab: void fn()
            called when drawing module's options tab.
    combat_local: void fn(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision)
            same as combat, but for chatbox events.
    wnd_filter: UINT fn(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
            return value is assigned to real uMsg.
            modifiers-filtered window event messages.
    options_windows: void fn(char* windowname)
            called during options window list, before the checkbox of 'window' is drawn.

  note: the data in arcdps_exports is copied after return, the original is not accessed thereafter.

---

arcdps module exports:

e0   returns wchar_t* path to arcdps ini.                               [ wchar_t* e0() ]

e3   logs str to arcdps.log.                                            [ void e3(char* str) ]

e5   writes colour array ptrs to *out, where *out is an ImVec4* out[5]. [ void e5(ImVec4** out) ]
       out[0] = core cols
                   enum n_colours_core {
                     CCOL_TRANSPARENT,
                     CCOL_WHITE,
                     CCOL_LWHITE,
                     CCOL_LGREY,
                     CCOL_LYELLOW,
                     CCOL_LGREEN,
                     CCOL_LRED,
                     CCOL_LTEAL,
                     CCOL_MGREY,
                     CCOL_DGREY,
                     CCOL_NUM
                   };
       out[1] = prof colours base
       out[2] = prof colours highlight
                   index for prof colour match prof enum, 0 unknown
       out[3] = subgroup colours base
       out[4] = subgroup colours highlight
                   index for subgroup colour matches subgroup, up to game max (update from combatenter statechange)

e6   returns bit mask of current ui settings.                           [ uint64_t e6() ]
       0 - ui is hidden
       1 - ui drawn always
       2 - ui is modifier move locked
       3 - ui is modifier click locked
       4 - ui is closing windows with esc

e7   returns modifier virtual key ids in word split.                   [ uint64_t e7() ]
       0-1 - mod1
       2-3 - mod2
       4-5 - modmulti

e8   logs str to logger window, extension tab                          [ void e8(char* str) ]
       colours are html-like, <c=#aaaaaa>coloured text</c>

e9   adds ev to arc's event processing                                 [ void e9(cbtevent* ev, uint32_t sig) ]
       is_statechange will be set to CBTS_EXTENSION, pad61-64 will be set to sig
       events will end up in the ringbuffer and sent along the realtime api

e10  adds ev to arc's event processing (with skill processing)         [ void e10(cbtevent* ev, uint32_t sig) ]
       is_statechange will be set to CBTS_EXTENSIONCOMBAT, pad61-64 will be set to sig
       same as e9, however, skillid will be treated as skillid and the skill will be added to the evtc skill table

addextension2   loads an extension                                     [ HINSTANCE hinst ]
        arcdps will loadlibrary hinst to increment refcount, call get_init_addr, and call it's returned function
        return value will be 0 on success, or non-zero on error:

/* arcdps api extension load result */
enum n_arcdpsextensionload {
	ARCDPSEXTENLOAD_OK,
	ARCDPSEXTENLOAD_NO_SIG,
	ARCDPSEXTENLOAD_INVALID_IMGUI, // extension will stay loaded with imgui functions disabled
	ARCDPSEXTENLOAD_OBSOLETE,
	ARCDPSEXTENLOAD_ALREADY_LOADED,
	ARCDPSEXTENLOAD_NO_FUNCTION_TABLE_RETURNED,
	ARCDPSEXTENLOAD_NO_INIT_FUNCTION_RETURNED,
	ARCDPSEXTENLOAD_LOADLIBRARY_ERROR,
	ARCDPSEXTENLOAD_NO_SLOTS_LEFT,
	ARCDPSEXTENLOAD_MISSING_GET_RELEASE_ADDR,
	ARCDPSEXTENLOAD_SHUTDOWN,
	ARCDPSEXTENLOAD_REMOVE_VIA_EXPORT
};

removeextension2   removes an extension                                  [ uint32_t sig ]
        arcdps will freelibrary, call the get_release_addr, and call it's returned function
        upon returning from freeextension there will be no more pending callbacks, however caller must ensure no callbacks are executing before freeing
        return value will be 0 if module was not found or HINSTANCE of the dll otherwise

listextension   callback list of extensions                            [ void* callback_fn ]
        callback_fn is of type "void callback_fn(arcdps_exports* exp)"
        callback is called once for each extension currently loaded

