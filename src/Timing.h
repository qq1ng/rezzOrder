#pragma once

#include <chrono>

// Finding a hitch after the fact. A game stall that comes and goes can't be caught in a debugger, so the
// addon times the separate steps of its own callbacks and keeps the slowest one. When a whole callback runs
// long enough to be felt in game, the caller logs which step it was.
//
// Only time steps that don't contain each other: a step around other steps is always the slowest one and
// says nothing.
namespace Timing
{
	struct Slowest
	{
		const char* Name = "";
		double      Ms   = 0.0;
	};

	// Per thread: the render thread and the arcdps threads each keep their own.
	inline Slowest& Current()
	{
		static thread_local Slowest s_Slowest;
		return s_Slowest;
	}

	inline void Reset() { Current() = Slowest{}; }

	class Step
	{
	public:
		explicit Step(const char* aName) : m_Name(aName), m_Start(std::chrono::steady_clock::now()) {}

		~Step()
		{
			double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
			Slowest& slowest = Current();
			if (ms > slowest.Ms) { slowest = Slowest{ m_Name, ms }; }
		}

		Step(const Step&) = delete;
		Step& operator=(const Step&) = delete;

	private:
		const char*                           m_Name;
		std::chrono::steady_clock::time_point m_Start;
	};
}
