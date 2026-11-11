#include "TestFramework.h"

#include <format>
#include <print>
#include <vector>

namespace Tests {

	struct TestCase
	{
		const char* Name;
		std::function<void()> Run;
	};

	// Function-local static so that registration from other translation units
	// during static initialization always sees a constructed registry.
	static std::vector<TestCase>& Registry()
	{
		static std::vector<TestCase> registry;
		return registry;
	}

	static int s_FailedChecks = 0;

	void Register(const char* name, std::function<void()> run)
	{
		Registry().push_back({ name, std::move(run) });
	}

	void ReportFailure(const char* file, int line, const std::string& message)
	{
		s_FailedChecks++;
		std::println("         {}:{}: {}", file, line, message);
	}

	bool IsNear(float actual, float expected, float tolerance)
	{
		return std::fabs(actual - expected) <= tolerance;
	}

	bool IsNear(const glm::vec3& actual, const glm::vec3& expected, float tolerance)
	{
		return IsNear(actual.x, expected.x, tolerance)
			&& IsNear(actual.y, expected.y, tolerance)
			&& IsNear(actual.z, expected.z, tolerance);
	}

	std::string DescribeNear(const char* expression, float actual, float expected, float tolerance)
	{
		return std::format("{} = {}, expected {} (tolerance {})", expression, actual, expected, tolerance);
	}

	std::string DescribeNear(const char* expression, const glm::vec3& actual, const glm::vec3& expected, float tolerance)
	{
		return std::format("{} = ({}, {}, {}), expected ({}, {}, {}) (tolerance {})",
			expression,
			actual.x, actual.y, actual.z,
			expected.x, expected.y, expected.z,
			tolerance);
	}

	int RunAll()
	{
		int failedTests = 0;

		for (const TestCase& test : Registry())
		{
			s_FailedChecks = 0;

			test.Run();

			if (s_FailedChecks == 0)
			{
				std::println("[  OK  ] {}", test.Name);
			}
			else
			{
				std::println("[ FAIL ] {} ({} check(s) failed)", test.Name, s_FailedChecks);
				failedTests++;
			}
		}

		const size_t total = Registry().size();
		std::println("");
		std::println("{} / {} tests passed", total - failedTests, total);

		return failedTests == 0 ? 0 : 1;
	}

}
