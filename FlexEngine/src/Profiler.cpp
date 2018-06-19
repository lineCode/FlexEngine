#include "stdafx.hpp"

#include "Profiler.hpp"

#include "Time.hpp"

namespace flex
{
	i32 Profiler::s_UnendedTimings = 0;
	ms Profiler::s_FrameStartTime = 0;
	ms Profiler::s_FrameEndTime = 0;
	std::map<std::string, ms> Profiler::s_Timings;
	std::string Profiler::s_PendingCSV;

	void Profiler::StartFrame()
	{
		s_Timings.clear();
		s_UnendedTimings = 0;
		s_FrameStartTime = Time::CurrentMilliseconds();
	}

	void Profiler::EndFrame(bool bPrintTimings)
	{
		s_FrameEndTime = Time::CurrentMilliseconds();

		if (s_UnendedTimings != 0)
		{
			Logger::LogError("Uneven number of profile blocks!");
		}

		if (bPrintTimings)
		{
			s_PendingCSV.append(std::to_string(s_FrameEndTime - s_FrameStartTime) + ",");

			//Logger::LogInfo("Profiler results:");
			//Logger::LogInfo("Whole frame: " + std::to_string(s_FrameEndTime - s_FrameStartTime) + "ms");
			//Logger::LogInfo("---");
			for (auto element : s_Timings)
			{
				//s_PendingCSV.append(std::string(element.first) + "," +
				//					std::to_string(element.second) + '\n');

				//Logger::LogInfo(std::string(element.first) + ": " + 
				//				std::to_string(element.second) + "ms");
			}
		}
	}

	void Profiler::Begin(const std::string& blockName)
	{
		if (s_Timings.find(blockName) != s_Timings.end())
		{
			Logger::LogError("Profiler::Begin called more than once!");
		}

		ms now = Time::CurrentMilliseconds();
		s_Timings.insert({ blockName, now });

		++s_UnendedTimings;
	}

	void Profiler::End(const std::string& blockName)
	{
		auto result = s_Timings.find(blockName);

		if (result == s_Timings.end())
		{
			Logger::LogError("Profiler::End called before Begin was called!");
		}

		ms now = Time::CurrentMilliseconds();
		ms start = result->second;
		ms elapsed = now - start;
		s_Timings.at(blockName) = elapsed;

		--s_UnendedTimings;
	}

	void Profiler::PrintResultsToFile()
	{
		std::string directory = RESOURCE_LOCATION + "profiles/";
		std::string absoluteDirectory = RelativePathToAbsolute(directory);
		CreateDirectoryRecursive(absoluteDirectory);

		std::string filePath = absoluteDirectory + "frame_times.csv";

		if (WriteFile(filePath, s_PendingCSV, false))
		{
			Logger::LogInfo("Wrote profiling results to " + filePath);
		}
		else
		{
			Logger::LogInfo("Failed to write profiling results to " + filePath);
		}
	}
} // namespace flex
