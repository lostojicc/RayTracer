#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <functional>
#include <string>

namespace Tests {

	void Register(const char* name, std::function<void()> run);
	void ReportFailure(const char* file, int line, const std::string& message);

	int RunAll();

	struct Registrar
	{
		Registrar(const char* name, std::function<void()> run)
		{
			Register(name, std::move(run));
		}
	};

	bool IsNear(float actual, float expected, float tolerance);
	bool IsNear(const glm::vec3& actual, const glm::vec3& expected, float tolerance);

	std::string DescribeNear(const char* expression, float actual, float expected, float tolerance);
	std::string DescribeNear(const char* expression, const glm::vec3& actual, const glm::vec3& expected, float tolerance);

}

#define TEST_CASE(name)                                                        \
	static void name();                                                        \
	static ::Tests::Registrar s_Registrar_##name(#name, name);                 \
	static void name()

#define CHECK(condition)                                                       \
	do                                                                         \
	{                                                                          \
		if (!(condition))                                                      \
			::Tests::ReportFailure(__FILE__, __LINE__, "expected: " #condition);\
	} while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                \
	do                                                                         \
	{                                                                          \
		const auto checkActual = (actual);                                     \
		const auto checkExpected = (expected);                                 \
		if (!::Tests::IsNear(checkActual, checkExpected, (tolerance)))          \
			::Tests::ReportFailure(__FILE__, __LINE__,                         \
				::Tests::DescribeNear(#actual, checkActual, checkExpected, (tolerance))); \
	} while (false)
