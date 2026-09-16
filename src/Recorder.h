#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Column layout of the Phase 0 CSV. Every row uses the same columns; unused ones stay empty.
enum Column
{
	COL_KIND,             // ARC, AGENT, UE_SQUAD, UE_CHAT, KEY, MARK, MAP, INFO
	COL_CHANNEL,          // SQUAD or LOCAL for ARC rows
	COL_ARRIVE_MS,        // timeGetTime() when the addon received the row
	COL_EVENT_MS,         // cbtevent.time (timeGetTime() when arcdps registered it)
	COL_DELAY_MS,         // arrive - event
	COL_ARC_ID,
	COL_STATECHANGE,
	COL_STATECHANGE_NAME,
	COL_ACTIVATION,
	COL_ACTIVATION_NAME,
	COL_VERDICT,
	COL_SKILL_ID,
	COL_SKILL_NAME,
	COL_REVIVE_SKILL,
	COL_VALUE,
	COL_BUFF_DMG,
	COL_OVERSTACK,
	COL_RESULT,
	COL_IFF,
	COL_BUFF_REMOVE,
	COL_SRC_ID,
	COL_SRC_NAME,
	COL_SRC_PROF,
	COL_SRC_ELITE,
	COL_SRC_SELF,
	COL_SRC_ACCOUNT,      // account name: the stable player identity (character names can be rank names)
	COL_SRC_INST,
	COL_SRC_MASTER_INST,
	COL_DST_ID,
	COL_DST_NAME,
	COL_DST_ACCOUNT,
	COL_DST_INST,
	COL_DETAIL,
	COL_TEXT,
	COL_COUNT
};

using CsvRow = std::array<std::string, COL_COUNT>;

// Appends CSV rows to a file from any thread; a background thread does the disk writes.
// Files are split into parts ("name_part2.csv", ...) so a long unattended session can't grow one file forever.
class Recorder
{
public:
	static constexpr uint64_t kPartBytes = 200ull * 1024 * 1024;

	~Recorder();

	bool Start(const std::filesystem::path& aFile);
	void Stop();
	void Write(const CsvRow& aRow);

	std::string Path() const;
	uint64_t RowCount() const { return m_Rows.load(); }
	uint64_t BytesWritten() const { return m_Bytes.load(); }

private:
	void Run();
	bool OpenPart(); // caller holds no lock; only Start() and the writer thread call this

	std::filesystem::path    m_BasePath;
	int                      m_Part = 1;
	uint64_t                 m_PartBytes = 0; // writer thread only
	mutable std::mutex       m_PathMutex;
	std::string              m_Path;
	std::FILE*               m_File = nullptr;
	std::thread              m_Thread;
	std::mutex               m_Mutex;
	std::condition_variable  m_Wake;
	std::vector<std::string> m_Pending;
	bool                     m_Stopping = false;
	std::atomic<uint64_t>    m_Rows{0};
	std::atomic<uint64_t>    m_Bytes{0};
};
