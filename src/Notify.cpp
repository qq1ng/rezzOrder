#include "Notify.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <Windows.h>
#include <mmsystem.h>

#include "imgui/imgui.h"

#include "Settings.h"

namespace Notify
{
	namespace
	{
		// The same state twice inside this long is one event as far as the player is concerned: being downed
		// and rallying, or a cooldown ahead of us expiring and then being spent, both bounce the turn.
		constexpr unsigned kRepeatGuardMs = 10 * 1000;
		constexpr unsigned kFlashInMs     = 150;
		constexpr unsigned kFlashOutMs    = 600;

		constexpr int   kSampleRate = 44100;
		constexpr short kChannels   = 1;

		Standing s_Last      = Standing::None;
		unsigned s_LastMs[3] = {};   // when each standing was last announced
		Standing s_Flash     = Standing::None;
		unsigned s_FlashMs   = 0;

		// PlaySound reads the buffer while it plays, so the bytes have to outlive the call.
		std::vector<unsigned char> s_Playing;

		void Append(std::vector<unsigned char>& aOut, const void* aData, size_t aSize)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(aData);
			aOut.insert(aOut.end(), bytes, bytes + aSize);
		}

		void AppendChunk(std::vector<unsigned char>& aOut, const char* aTag, uint32_t aSize)
		{
			Append(aOut, aTag, 4);
			Append(aOut, &aSize, 4);
		}

		// A 16-bit mono WAV around the samples, so PlaySound can take it straight from memory.
		std::vector<unsigned char> Wrap(const std::vector<short>& aSamples)
		{
			std::vector<unsigned char> wav;
			uint32_t dataBytes = static_cast<uint32_t>(aSamples.size() * sizeof(short));
			AppendChunk(wav, "RIFF", 36 + dataBytes);
			Append(wav, "WAVE", 4);
			AppendChunk(wav, "fmt ", 16);
			uint16_t format = 1, channels = kChannels, bits = 16;
			uint32_t rate = kSampleRate, bytesPerSecond = kSampleRate * 2;
			uint16_t blockAlign = 2;
			Append(wav, &format, 2);
			Append(wav, &channels, 2);
			Append(wav, &rate, 4);
			Append(wav, &bytesPerSecond, 4);
			Append(wav, &blockAlign, 2);
			Append(wav, &bits, 2);
			AppendChunk(wav, "data", dataBytes);
			Append(wav, aSamples.data(), dataBytes);
			return wav;
		}

		float Envelope(float aT, float aLength, float aAttack)
		{
			if (aT < aAttack) { return aT / aAttack; }              // no click at the start
			float rest = (aT - aAttack) / std::max(aLength - aAttack, 0.0001f);
			return std::exp(-4.0f * rest);                          // fades out
		}

		// Each sound is built from scratch at the volume in the settings; there are no audio files to ship
		// and no mixer to configure.
		std::vector<short> Samples(Sound aSound, float aGain)
		{
			std::vector<short> samples;
			auto tone = [&](float aFrom, float aTo, float aLength, float aLevel, float aAttack = 0.006f)
			{
				int count = static_cast<int>(aLength * kSampleRate);
				float phase = 0.0f;
				for (int i = 0; i < count; i++)
				{
					float t = static_cast<float>(i) / kSampleRate;
					float frequency = aFrom + (aTo - aFrom) * (t / std::max(aLength, 0.0001f));
					phase += 2.0f * 3.14159265f * frequency / kSampleRate;
					float value = std::sin(phase) * Envelope(t, aLength, aAttack) * aLevel * aGain;
					samples.push_back(static_cast<short>(std::clamp(value, -1.0f, 1.0f) * 32000.0f));
				}
			};
			auto silence = [&](float aLength)
			{
				samples.insert(samples.end(), static_cast<size_t>(aLength * kSampleRate), 0);
			};

			switch (aSound)
			{
				case Sound::SoftChime:  tone(880.0f, 880.0f, 0.16f, 0.5f); tone(1320.0f, 1320.0f, 0.34f, 0.4f); break;
				case Sound::DoubleBeep: tone(1000.0f, 1000.0f, 0.07f, 0.55f); silence(0.05f); tone(1000.0f, 1000.0f, 0.07f, 0.55f); break;
				case Sound::RisingBlip: tone(600.0f, 1500.0f, 0.18f, 0.55f); break;
				case Sound::LowThud:    tone(150.0f, 90.0f, 0.26f, 0.85f, 0.002f); break;
				case Sound::Tick:       tone(2200.0f, 1800.0f, 0.02f, 0.5f, 0.001f); break;
				default: break;
			}
			return samples;
		}

		ImVec4 FlashColor(Standing aStanding)
		{
			const Settings::Values& s = Settings::Current;
			const float* rgb = aStanding == Standing::Up ? s.UpFlashColor : s.BackupFlashColor;
			return ImVec4(rgb[0], rgb[1], rgb[2], 1.0f);
		}

		bool FlashEnabled(Standing aStanding)
		{
			return aStanding == Standing::Up ? Settings::Current.UpFlash : Settings::Current.BackupFlash;
		}

		Sound SoundFor(Standing aStanding)
		{
			int value = aStanding == Standing::Up ? Settings::Current.UpSound : Settings::Current.BackupSound;
			return value > 0 && value < static_cast<int>(Sound::Count) ? static_cast<Sound>(value) : Sound::None;
		}
	}

	const char* SoundName(Sound aSound)
	{
		switch (aSound)
		{
			case Sound::None:       return "none";
			case Sound::SoftChime:  return "soft chime";
			case Sound::DoubleBeep: return "double beep";
			case Sound::RisingBlip: return "rising blip";
			case Sound::LowThud:    return "low thud";
			case Sound::Tick:       return "tick";
			case Sound::File:       return "file...";
			default:                return "?";
		}
	}

	void Init() {}

	void Reset()
	{
		s_Last = Standing::None;
		s_Flash = Standing::None;
		s_FlashMs = 0;
		s_LastMs[0] = s_LastMs[1] = s_LastMs[2] = 0;
	}

	void Shutdown()
	{
		PlaySoundW(nullptr, nullptr, 0);
		s_Playing.clear();
	}

	void Play(Sound aSound)
	{
		if (aSound == Sound::None) { return; }
		float gain = std::clamp(Settings::Current.SoundVolume / 100.0f, 0.0f, 1.0f);
		if (gain <= 0.0f) { return; }

		if (aSound == Sound::File)
		{
			if (Settings::Current.SoundFile.empty()) { return; }
			int size = MultiByteToWideChar(CP_UTF8, 0, Settings::Current.SoundFile.c_str(), -1, nullptr, 0);
			std::wstring path(size > 0 ? size - 1 : 0, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, Settings::Current.SoundFile.c_str(), -1, path.data(), size);
			PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
			return;
		}

		std::vector<short> samples = Samples(aSound, gain);
		if (samples.empty()) { return; }
		// Stop whatever was playing before the old buffer goes away underneath it.
		PlaySoundW(nullptr, nullptr, 0);
		s_Playing = Wrap(samples);
		PlaySoundA(reinterpret_cast<const char*>(s_Playing.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
	}

	std::string OnStanding(Standing aStanding, unsigned aNowMs, bool aInWvw, bool aGameplay)
	{
		if (!aGameplay || !aInWvw)
		{
			// Keep following the state so that coming back to a WvW map doesn't announce a standing the
			// player has had for the last ten minutes.
			s_Last = aStanding;
			return {};
		}
		if (aStanding == s_Last) { return {}; }
		s_Last = aStanding;
		if (aStanding == Standing::None) { return {}; }

		unsigned& last = s_LastMs[static_cast<int>(aStanding)];
		if (last != 0 && aNowMs - last < kRepeatGuardMs) { return {}; } // the turn bounced; say it once
		last = aNowMs;

		Play(SoundFor(aStanding));
		if (FlashEnabled(aStanding))
		{
			s_Flash = aStanding;
			s_FlashMs = aNowMs;
		}
		if (aStanding == Standing::Up && Settings::Current.UpBanner) { return "You're up"; }
		if (aStanding == Standing::Backup && Settings::Current.BackupBanner) { return "Backup"; }
		return {};
	}

	void Render(unsigned aNowMs, bool aGameplay)
	{
		if (s_Flash == Standing::None) { return; }
		unsigned age = aNowMs - s_FlashMs;
		if (!aGameplay || age > kFlashInMs + kFlashOutMs) { s_Flash = Standing::None; return; }

		// One pulse: quick in, slower out.
		float strength = age < kFlashInMs
			? static_cast<float>(age) / kFlashInMs
			: 1.0f - static_cast<float>(age - kFlashInMs) / kFlashOutMs;
		strength = std::clamp(strength, 0.0f, 1.0f) * std::clamp(Settings::Current.FlashStrength, 0.0f, 1.0f);
		if (strength <= 0.001f) { return; }

		ImVec4 color = FlashColor(s_Flash);
		ImU32 edge = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, strength));
		ImU32 clear = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.0f));

		// A band along each edge, fading inwards: the game stays readable in the middle, and the corners
		// where the bands overlap glow strongest, which is where the eye catches it.
		ImVec2 screen = ImGui::GetIO().DisplaySize;
		float depth = std::max(40.0f, std::min(screen.x, screen.y) * 0.13f);
		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x, depth), edge, edge, clear, clear);
		draw->AddRectFilledMultiColor(ImVec2(0, screen.y - depth), ImVec2(screen.x, screen.y), clear, clear, edge, edge);
		draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(depth, screen.y), edge, clear, clear, edge);
		draw->AddRectFilledMultiColor(ImVec2(screen.x - depth, 0), ImVec2(screen.x, screen.y), clear, edge, edge, clear);
	}
}
