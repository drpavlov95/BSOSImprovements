// Runner de testes minimo. Cada test_*.cpp se auto-registra via TEST(nome).
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

struct TestCase {
	const char* name;
	bool (*fn)();
};

inline std::vector<TestCase>& TestRegistry() {
	static std::vector<TestCase> reg;
	return reg;
}

struct TestRegistrar {
	TestRegistrar(const char* name, bool (*fn)()) { TestRegistry().push_back({name, fn}); }
};

#define TEST(name)                                     \
	static bool name();                                \
	static TestRegistrar test_reg_##name(#name, name); \
	static bool name()

#define TEST_ASSERT(expr)                                                                   \
	do {                                                                                    \
		if (!(expr)) {                                                                      \
			std::printf("\n      FALHOU: %s\n      em %s:%d\n", #expr, __FILE__, __LINE__); \
			return false;                                                                   \
		}                                                                                   \
	} while (0)
