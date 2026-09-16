#include "Recorder.h"

#include <chrono>
#include <string_view>

namespace
{
	constexpr const char* kHeader[COL_COUNT] = {
		"kind", "channel", "arrive_ms", "event_ms", "delay_ms", "arc_id", "statechange", "statechange_name",
		"activation", "activation_name", "verdict", "skill_id", "skill_name", "revive_skill", "value", "buff_dmg",
		"overstack", "result", "iff", "buff_remove", "src_id", "src_name", "src_prof", "src_elite", "src_self",
		"src_account", "src_inst", "src_master_inst", "dst_id", "dst_name", "dst_account", "dst_inst", "detail", "text"
	};

	void AppendField(std::string& aLine, std::string_view aField)
	{
		if (aField.find_first_of(",\"\r\n") == std::string_view::npos)
		{
			aLine.append(aField);
			return;
		}
		aLine.push_back('"');
		for (char c : aField)
		{
			if (c == '"') { aLine.push_back('"'); }
			aLine.push_back(c);
		}
		aLine.push_back('"');
	}
}

Recorder::~Recorder()
{
	Stop();
}

bool Recorder::Start(const std::filesystem::path& aFile)
{
	Stop();

	std::error_code ec;
	std::filesystem::create_directories(aFile.parent_path(), ec);

	m_BasePath = aFile;
	m_Part = 1;
	m_Rows = 0;
	m_Bytes = 0;
	m_Stopping = false;
	if (!OpenPart()) { return false; }

	m_Thread = std::thread(&Recorder::Run, this);
	return true;
}

bool Recorder::OpenPart()
{
	std::filesystem::path path = m_BasePath;
	if (m_Part > 1)
	{
		path.replace_filename(m_BasePath.stem().wstring() + L"_part" + std::to_wstring(m_Part) + m_BasePath.extension().wstring());
	}

	std::FILE* file = nullptr;
	if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) { return false; }

	std::string header;
	for (int i = 0; i < COL_COUNT; i++)
	{
		if (i > 0) { header.push_back(','); }
		header.append(kHeader[i]);
	}
	header.append("\r\n");
	std::fwrite(header.data(), 1, header.size(), file);

	m_File = file;
	m_PartBytes = header.size();
	std::scoped_lock lock(m_PathMutex);
	m_Path = path.string();
	return true;
}

void Recorder::Stop()
{
	if (!m_Thread.joinable()) { return; }

	{
		std::scoped_lock lock(m_Mutex);
		m_Stopping = true;
	}
	m_Wake.notify_one();
	m_Thread.join();

	if (m_File) { std::fclose(m_File); }
	m_File = nullptr;
}

std::string Recorder::Path() const
{
	std::scoped_lock lock(m_PathMutex);
	return m_Path;
}

void Recorder::Write(const CsvRow& aRow)
{
	std::string line;
	line.reserve(256);
	for (int i = 0; i < COL_COUNT; i++)
	{
		if (i > 0) { line.push_back(','); }
		AppendField(line, aRow[i]);
	}
	line.append("\r\n");

	{
		std::scoped_lock lock(m_Mutex);
		if (!m_Thread.joinable() || m_Stopping) { return; }
		m_Pending.push_back(std::move(line));
	}
	m_Rows++;
}

void Recorder::Run()
{
	std::vector<std::string> batch;
	for (;;)
	{
		bool stopping;
		{
			std::unique_lock lock(m_Mutex);
			// Batch writes; flushing every 250 ms keeps the file current if the game crashes.
			m_Wake.wait_for(lock, std::chrono::milliseconds(250), [this] { return m_Stopping; });
			batch.swap(m_Pending);
			stopping = m_Stopping;
		}

		for (const std::string& line : batch)
		{
			if (m_File == nullptr) { break; }
			std::fwrite(line.data(), 1, line.size(), m_File);
			m_PartBytes += line.size();
			m_Bytes += line.size();

			if (m_PartBytes >= kPartBytes)
			{
				std::fclose(m_File);
				m_File = nullptr;
				m_Part++;
				OpenPart(); // on failure m_File stays null and further rows are dropped
			}
		}
		if (m_File && !batch.empty()) { std::fflush(m_File); }
		batch.clear();

		if (stopping) { return; }
	}
}
