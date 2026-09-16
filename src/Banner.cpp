#include "Banner.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

#include "imgui/imgui.h"

#include "ArcStyle.h"
#include "Fonts.h"
#include "Settings.h"

namespace Banner
{
	namespace
	{
		constexpr unsigned kFadeInMs    = 120;
		constexpr unsigned kFadeOutMs   = 450;
		constexpr size_t   kMaxPerGroup = 4;     // past this the oldest message of a group gives way
		constexpr size_t   kMaxKept     = 12;
		constexpr float    kWrapShare   = 0.55f; // widest a message gets before it wraps, as a share of the width

		struct Message
		{
			Kind        What;
			std::string Text;
			unsigned    SinceMs;
		};

		struct Line
		{
			std::string Text;
			float       Pixels;
		};

		struct CountdownEntry
		{
			unsigned    Seconds;
			std::string Names;
		};

		std::deque<Message>         s_Messages;   // newest first
		std::vector<CountdownEntry> s_Countdowns; // this frame's, soonest first
		Area                        s_Last;

		bool IsAlert(Kind aKind) { return aKind == Kind::Up || aKind == Kind::Backup || aKind == Kind::Countdown; }

		unsigned ShowMs()
		{
			return static_cast<unsigned>(std::clamp(Settings::Current.BannerSeconds, 1.0f, 60.0f) * 1000.0f);
		}

		float InfoPixels()  { return Fonts::BannerBasePixels() * std::clamp(Settings::Current.BannerInfoSize, 0.5f, 5.0f); }
		float AlertPixels() { return Fonts::BannerBasePixels() * std::clamp(Settings::Current.BannerAlertSize, 0.5f, 5.0f); }

		ImVec4 Rgb(const float aColor[3]) { return ImVec4(aColor[0], aColor[1], aColor[2], 1.0f); }

		ImVec4 KindColor(Kind aKind)
		{
			const Settings::Values& s = Settings::Current;
			switch (aKind)
			{
				case Kind::Up:        return Rgb(s.BannerUpColor);
				case Kind::Backup:    return Rgb(s.BannerBackupColor);
				case Kind::Countdown: return Rgb(s.BannerIllusionColor);
				case Kind::Problem:   return ImVec4(1.00f, 0.46f, 0.40f, 1.0f); // the addon's own setup is broken: always red
				default:              return Rgb(s.BannerInfoColor);
			}
		}

		ImU32 Faded(ImVec4 aColor, float aAlpha)
		{
			aColor.w *= aAlpha;
			return ImGui::GetColorU32(aColor);
		}

		// 0 to 1 over a message's time on screen: in quickly, held, out more slowly.
		float Opacity(unsigned aAgeMs, unsigned aShowMs)
		{
			if (aAgeMs < kFadeInMs) { return static_cast<float>(aAgeMs) / kFadeInMs; }
			unsigned fadeFrom = aShowMs > kFadeOutMs ? aShowMs - kFadeOutMs : 0;
			if (aAgeMs < fadeFrom) { return 1.0f; }
			return 1.0f - std::min(1.0f, static_cast<float>(aAgeMs - fadeFrom) / kFadeOutMs);
		}

		// Text with a thin dark rim, so it reads over snow as well as over a night map.
		void RimmedText(ImDrawList* aDraw, ImFont* aFont, float aSize, ImVec2 aPos, ImU32 aColor, ImU32 aRim,
			const char* aText, float aWrap)
		{
			const float r = std::max(1.0f, std::round(aSize / 22.0f));
			for (float dx : { -r, 0.0f, r })
			{
				for (float dy : { -r, 0.0f, r })
				{
					if (dx == 0.0f && dy == 0.0f) { continue; }
					aDraw->AddText(aFont, aSize, ImVec2(aPos.x + dx, aPos.y + dy), aRim, aText, nullptr, aWrap);
				}
			}
			aDraw->AddText(aFont, aSize, aPos, aColor, aText, nullptr, aWrap);
		}

		float GlowOf(float aPixels) { return std::max(2.0f, std::round(aPixels * 0.14f)); }

		// A soft halo in the text's own colour, for the callout style.
		void GlowText(ImDrawList* aDraw, ImFont* aFont, float aSize, ImVec2 aPos, ImVec4 aColor, float aAlpha,
			const char* aText, float aWrap)
		{
			float glow = GlowOf(aSize);
			ImVec4 halo = aColor;
			halo.w = 0.10f;
			ImU32 haloColor = Faded(halo, aAlpha);
			for (int ring = 3; ring >= 1; ring--)
			{
				float r = glow * static_cast<float>(ring) / 3.0f;
				for (int step = 0; step < 12; step++)
				{
					float angle = static_cast<float>(step) * (2.0f * 3.14159265f / 12.0f);
					aDraw->AddText(aFont, aSize, ImVec2(aPos.x + std::cos(angle) * r, aPos.y + std::sin(angle) * r),
						haloColor, aText, nullptr, aWrap);
				}
			}
		}

		// Lines laid out one under another, each centred in the width of the widest.
		struct Block
		{
			std::vector<Line>   Lines;
			std::vector<ImVec2> Sizes;
			ImVec2              Size;
			float               Gap      = 0.0f;
			float               Smallest = FLT_MAX; // paddings follow the smallest text, not a huge countdown figure
			float               Largest  = 0.0f;
		};

		Block Measure(ImFont* aFont, const std::vector<Line>& aLines, float aWrap)
		{
			Block block;
			block.Lines = aLines;
			for (const Line& line : aLines)
			{
				block.Smallest = std::min(block.Smallest, line.Pixels);
				block.Largest = std::max(block.Largest, line.Pixels);
			}
			block.Gap = aLines.size() > 1 ? std::round(block.Smallest * 0.15f) : 0.0f;
			for (size_t i = 0; i < aLines.size(); i++)
			{
				ImVec2 size = aFont->CalcTextSizeA(aLines[i].Pixels, FLT_MAX, aWrap, aLines[i].Text.c_str());
				size.x = std::ceil(size.x);
				size.y = std::ceil(size.y);
				block.Sizes.push_back(size);
				block.Size.x = std::max(block.Size.x, size.x);
				block.Size.y += size.y + (i > 0 ? block.Gap : 0.0f);
			}
			return block;
		}

		void DrawBlock(ImDrawList* aDraw, ImFont* aFont, const Block& aBlock, ImVec2 aPos, ImVec4 aColor, float aAlpha,
			float aWrap, Style aStyle)
		{
			ImU32 rim = Faded(ImVec4(0.02f, 0.02f, 0.03f, 0.85f), aAlpha);
			float y = aPos.y;
			for (size_t i = 0; i < aBlock.Lines.size(); i++)
			{
				const Line& line = aBlock.Lines[i];
				ImVec2 at(aPos.x + std::round((aBlock.Size.x - aBlock.Sizes[i].x) * 0.5f), y);
				if (aStyle == Style::Callout) { GlowText(aDraw, aFont, line.Pixels, at, aColor, aAlpha, line.Text.c_str(), aWrap); }
				if (aStyle == Style::Callout || aStyle == Style::Text)
				{
					RimmedText(aDraw, aFont, line.Pixels, at, Faded(aColor, aAlpha), rim, line.Text.c_str(), aWrap);
				}
				else
				{
					aDraw->AddText(aFont, line.Pixels, at, Faded(aColor, aAlpha), line.Text.c_str(), nullptr, aWrap);
				}
				y += aBlock.Sizes[i].y + aBlock.Gap;
			}
		}

		// Measures or draws one message with its top-left corner at aPos, and returns the size it takes up.
		// Measuring and drawing share this, so the two can never disagree about the layout.
		ImVec2 Layout(ImDrawList* aDraw, Kind aKind, std::vector<Line> aLines, ImVec2 aPos, float aAlpha, float aWrap)
		{
			ImFont* font = Fonts::BannerFont();
			Style style = static_cast<Style>(std::clamp(Settings::Current.BannerStyle, 0, static_cast<int>(Style::Count) - 1));
			ImVec4 color = KindColor(aKind);

			switch (style)
			{
				case Style::Text:
				{
					Block block = Measure(font, aLines, aWrap);
					ImVec2 box(block.Size.x + 4.0f, block.Size.y + 4.0f);
					if (aDraw) { DrawBlock(aDraw, font, block, ImVec2(aPos.x + 2.0f, aPos.y + 2.0f), color, aAlpha, aWrap, style); }
					return box;
				}

				case Style::Callout:
				{
					Block block = Measure(font, aLines, aWrap);
					float glow = GlowOf(block.Largest);
					ImVec2 box(block.Size.x + glow * 2, block.Size.y + glow * 2);
					if (aDraw) { DrawBlock(aDraw, font, block, ImVec2(aPos.x + glow, aPos.y + glow), color, aAlpha, aWrap, style); }
					return box;
				}

				case Style::Window:
				{
					// The look of an ImGui window: ArcDPS' colours when the overlay matches them, a title line saying
					// where the message comes from, the message under it.
					constexpr const char kPrefix[] = "Rezz Order ";
					if (aLines.size() == 1 && aLines[0].Text.rfind(kPrefix, 0) == 0) { aLines[0].Text.erase(0, sizeof(kPrefix) - 1); }
					Block block = Measure(font, aLines, aWrap);
					const ImGuiStyle& imgui = ImGui::GetStyle();
					const ArcStyle::Values& arc = ArcStyle::Get();
					bool matchArc = Settings::Current.MatchArcDps && arc.Ok;
					auto colorOf = [&](ImGuiCol aCol) { return matchArc && arc.HasColor[aCol] ? arc.Colors[aCol] : imgui.Colors[aCol]; };
					float titleSize = std::min(InfoPixels(), block.Smallest) * 0.72f;
					const char* title = "Rezz Order";
					ImVec2 titleText = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title);
					float pad = std::round(block.Smallest * 0.45f);
					float titleHeight = std::round(titleText.y + pad * 0.6f);
					ImVec2 box(std::max(block.Size.x, titleText.x) + pad * 2, titleHeight + block.Size.y + pad * 1.4f);
					if (aDraw)
					{
						float rounding = matchArc ? arc.WindowRounding : imgui.WindowRounding;
						ImVec2 max(aPos.x + box.x, aPos.y + box.y);
						ImVec4 bg = colorOf(ImGuiCol_WindowBg);
						bg.w = std::max(bg.w, 0.88f);
						aDraw->AddRectFilled(aPos, max, Faded(bg, aAlpha), rounding);
						aDraw->AddRectFilled(aPos, ImVec2(max.x, aPos.y + titleHeight), Faded(colorOf(ImGuiCol_TitleBgActive), aAlpha),
							rounding, ImDrawCornerFlags_Top);
						aDraw->AddRect(aPos, max, Faded(colorOf(ImGuiCol_Border), aAlpha), rounding);
						aDraw->AddText(font, titleSize, ImVec2(aPos.x + pad, aPos.y + (titleHeight - titleText.y) * 0.5f),
							Faded(colorOf(ImGuiCol_Text), aAlpha * 0.8f), title);
						float left = aPos.x + pad + std::round((box.x - pad * 2 - block.Size.x) * 0.5f);
						DrawBlock(aDraw, font, block, ImVec2(left, aPos.y + titleHeight + pad * 0.7f), color, aAlpha, aWrap, style);
					}
					return box;
				}

				case Style::Plate:
				default:
				{
					// A dark plate with the colour down its left edge: calm to look at, and the colour still says what
					// it is about before the words do.
					Block block = Measure(font, aLines, aWrap);
					float pad = std::round(block.Smallest * 0.5f);
					float bar = std::max(3.0f, std::round(block.Smallest * 0.14f));
					ImVec2 box(bar + pad * 2 + block.Size.x, pad * 1.6f + block.Size.y);
					if (aDraw)
					{
						ImVec2 max(aPos.x + box.x, aPos.y + box.y);
						float rounding = std::round(block.Smallest * 0.18f);
						aDraw->AddRectFilled(aPos, max, Faded(ImVec4(0.07f, 0.08f, 0.10f, 0.86f), aAlpha), rounding);
						aDraw->AddRectFilled(aPos, ImVec2(aPos.x + bar, max.y), Faded(color, aAlpha), rounding, ImDrawCornerFlags_Left);
						aDraw->AddRect(aPos, max, Faded(ImVec4(1.0f, 1.0f, 1.0f, 0.08f), aAlpha), rounding);
						DrawBlock(aDraw, font, block, ImVec2(aPos.x + bar + pad, aPos.y + pad * 0.8f), color, aAlpha, aWrap, style);
					}
					return box;
				}
			}
		}

		std::vector<Line> LinesOf(const Message& aMessage)
		{
			return { Line{ aMessage.Text, IsAlert(aMessage.What) ? AlertPixels() : InfoPixels() } };
		}

		// The figures are what has to be read at a glance, so they are the big line. Which of the two comes first
		// is a setting: under the names puts the figures nearer the middle of the screen, where the eyes are, and
		// further from the target's name and effects at the top.
		std::vector<Line> CountdownLines(const CountdownEntry& aEntry)
		{
			char figures[16];
			std::snprintf(figures, sizeof(figures), "%u", aEntry.Seconds);
			Line number{ figures, AlertPixels() * kCountdownNumberScale };
			Line names{ aEntry.Names, AlertPixels() * kCountdownNameScale };
			if (Settings::Current.IllusionNumberBelow) { return { names, number }; }
			return { number, names };
		}
	}

	const char* StyleName(Style aStyle)
	{
		switch (aStyle)
		{
			case Style::Text:    return "text";
			case Style::Plate:   return "plate";
			case Style::Window:  return "window";
			case Style::Callout: return "callout";
			default:             return "?";
		}
	}

	void Show(Kind aKind, const std::string& aText, unsigned aNowMs)
	{
		if (aText.empty()) { return; }
		for (auto it = s_Messages.begin(); it != s_Messages.end(); ++it)
		{
			if (it->What == aKind && it->Text == aText)
			{
				// Said again while still up: it moves back to the top and stays for another full time, rather than
				// showing twice.
				s_Messages.erase(it);
				break;
			}
		}
		s_Messages.push_front(Message{ aKind, aText, aNowMs });
		while (s_Messages.size() > kMaxKept) { s_Messages.pop_back(); }
	}

	void Countdown(unsigned aSeconds, const std::string& aNames)
	{
		s_Countdowns.push_back(CountdownEntry{ aSeconds, aNames });
	}

	void Render(unsigned aNowMs)
	{
		s_Last = Area{};
		unsigned showMs = ShowMs();
		while (!s_Messages.empty() && aNowMs - s_Messages.back().SinceMs >= showMs) { s_Messages.pop_back(); }
		std::vector<CountdownEntry> countdowns;
		countdowns.swap(s_Countdowns); // set again by whoever still wants them next frame
		if (s_Messages.empty() && countdowns.empty()) { return; }

		ImVec2 screen = ImGui::GetIO().DisplaySize;
		if (screen.x <= 0.0f || screen.y <= 0.0f) { return; }
		// On top of every window, like the game's own messages, so a menu that happens to be open can't hide it.
		ImDrawList* draw = ImGui::GetForegroundDrawList();
		float wrap = std::round(screen.x * kWrapShare);
		float left = FLT_MAX, top = FLT_MAX, right = 0.0f, bottom = 0.0f;

		// Each group stacks down from its own place on screen, newest first.
		auto stack = [&](bool aAlerts)
		{
			const Settings::Values& s = Settings::Current;
			float centre = screen.x * std::clamp(aAlerts ? s.BannerAlertX : s.BannerInfoX, 0.0f, 100.0f) / 100.0f;
			float y = std::round(screen.y * std::clamp(aAlerts ? s.BannerAlertY : s.BannerInfoY, 0.0f, 100.0f) / 100.0f);
			float gap = std::round((aAlerts ? AlertPixels() : InfoPixels()) * 0.3f);

			auto place = [&](Kind aKind, const std::vector<Line>& aLines, float aAlpha)
			{
				ImVec2 box = Layout(nullptr, aKind, aLines, ImVec2(0.0f, 0.0f), aAlpha, wrap);
				// Kept on screen whatever the position setting says.
				float x = std::clamp(std::round(centre - box.x * 0.5f), 0.0f, std::max(0.0f, screen.x - box.x));
				ImVec2 pos(x, y);
				if (aAlpha > 0.0f) { Layout(draw, aKind, aLines, pos, aAlpha, wrap); }
				left = std::min(left, pos.x);
				right = std::max(right, pos.x + box.x);
				top = std::min(top, pos.y);
				bottom = std::max(bottom, pos.y + box.y);
				y += box.y + gap;
			};

			if (aAlerts && !countdowns.empty())
			{
				// Casts running out at the same time sit side by side, soonest on the left, the row centred as a
				// whole rather than one in the middle and the next pushed off to a side.
				std::vector<ImVec2> boxes;
				float between = gap * 2.0f;
				float total = 0.0f, tallest = 0.0f;
				for (const CountdownEntry& entry : countdowns)
				{
					ImVec2 box = Layout(nullptr, Kind::Countdown, CountdownLines(entry), ImVec2(0.0f, 0.0f), 1.0f, wrap);
					boxes.push_back(box);
					total += box.x;
					tallest = std::max(tallest, box.y);
				}
				total += between * static_cast<float>(countdowns.size() - 1);
				float x = std::clamp(std::round(centre - total * 0.5f), 0.0f, std::max(0.0f, screen.x - total));
				for (size_t i = 0; i < countdowns.size(); i++)
				{
					ImVec2 pos(x, y);
					Layout(draw, Kind::Countdown, CountdownLines(countdowns[i]), pos, 1.0f, wrap);
					left = std::min(left, pos.x);
					right = std::max(right, pos.x + boxes[i].x);
					top = std::min(top, pos.y);
					bottom = std::max(bottom, pos.y + boxes[i].y);
					x += boxes[i].x + between;
				}
				y += tallest + gap;
			}
			size_t shown = 0;
			for (const Message& message : s_Messages)
			{
				if (IsAlert(message.What) != aAlerts) { continue; }
				if (shown++ == kMaxPerGroup) { break; }
				place(message.What, LinesOf(message), Opacity(aNowMs - message.SinceMs, showMs));
			}
		};
		stack(false);
		stack(true);
		if (right > left) { s_Last = Area{ left, top, right - left, bottom - top }; }
	}

	void Clear()
	{
		s_Messages.clear();
		s_Countdowns.clear();
		s_Last = Area{};
	}

	Area LastArea()
	{
		return s_Last;
	}
}
