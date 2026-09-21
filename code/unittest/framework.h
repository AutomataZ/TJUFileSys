#pragma once
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>
#include <cstdlib>

// 极简测试框架: 无第三方依赖, 只提供用例注册、断言与统计三项功能
//
// 用例分四类, 后两个维度可以叠加:
//   UT_TEST                普通用例, 失败计入退出码
//   UT_TEST_KNOWN_BUG      已知缺陷用例, 断言的是"正确行为": 当前必然失败,
//                          因此失败才是预期结果, 不计入退出码;
//                          缺陷修复后会自动转绿并提示从名单移出
//   UT_TEST_FULL           仅在 --full 时运行的重量级用例
//   UT_TEST_FULL_KNOWN_BUG 重量级 + 已知缺陷

namespace ut {

struct TestCase {
    std::string suite;              // 套件名, 用于分组显示
    std::string name;               // 用例显示名
    std::function<void()> body;
    bool known_bug;
    bool needs_full;
};

// 函数内静态对象, 避免跨编译单元的静态初始化顺序问题
inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

// 当前正在执行的用例累积的失败信息
inline std::vector<std::string>& current_failures()
{
    static std::vector<std::string> f;
    return f;
}

inline void report_failure(const char* file, int line, const std::string& msg)
{
    std::ostringstream oss;
    oss << file << ":" << line << "  " << msg;
    current_failures().push_back(oss.str());
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body,
              bool known_bug, bool needs_full)
    {
        registry().push_back(TestCase{suite, name, body, known_bug, needs_full});
    }
};

// ---- 值转字符串 (供断言失败信息使用) ----
inline std::string to_str(const std::string& v) { return "\"" + v + "\""; }
inline std::string to_str(const char* v) { return v ? std::string("\"") + v + "\"" : "(null)"; }
inline std::string to_str(bool v) { return v ? "true" : "false"; }
inline std::string to_str(std::nullptr_t) { return "nullptr"; }
template <class T>
inline std::string to_str(const T& v)
{
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

} // namespace ut

// ---- 用例定义 ----
#define UT_TEST(suite, ident, display)                                          \
    static void ut_body_##suite##_##ident();                                    \
    static ::ut::Registrar ut_reg_##suite##_##ident(                            \
        #suite, display, ut_body_##suite##_##ident, false, false);              \
    static void ut_body_##suite##_##ident()

#define UT_TEST_KNOWN_BUG(suite, ident, display)                                \
    static void ut_body_##suite##_##ident();                                    \
    static ::ut::Registrar ut_reg_##suite##_##ident(                            \
        #suite, display, ut_body_##suite##_##ident, true, false);               \
    static void ut_body_##suite##_##ident()

#define UT_TEST_FULL(suite, ident, display)                                     \
    static void ut_body_##suite##_##ident();                                    \
    static ::ut::Registrar ut_reg_##suite##_##ident(                            \
        #suite, display, ut_body_##suite##_##ident, false, true);               \
    static void ut_body_##suite##_##ident()

// 重量级 + 已知缺陷: 只在 --full 时运行, 期望失败
#define UT_TEST_FULL_KNOWN_BUG(suite, ident, display)                           \
    static void ut_body_##suite##_##ident();                                    \
    static ::ut::Registrar ut_reg_##suite##_##ident(                            \
        #suite, display, ut_body_##suite##_##ident, true, true);                \
    static void ut_body_##suite##_##ident()

// ---- 断言 ----
// 断言失败只记录、不中断, 以便一次看到用例内的全部失败点
#define UT_CHECK(cond)                                                          \
    do {                                                                        \
        if (!(cond))                                                            \
            ::ut::report_failure(__FILE__, __LINE__,                            \
                                 std::string("断言失败: ") + #cond);            \
    } while (0)

// msg 两侧必须加括号: 调用处常写成 "a ? b : c" 形式的三目表达式,
// 而 + 的优先级高于 ?:, 不括起来会被解析成 ("..." + a) ? b : c
#define UT_CHECK_MSG(cond, msg)                                                 \
    do {                                                                        \
        if (!(cond))                                                            \
            ::ut::report_failure(__FILE__, __LINE__,                            \
                                 std::string("断言失败: ") + #cond + "  (" + (msg) + ")"); \
    } while (0)

#define UT_CHECK_EQ(actual, expected)                                           \
    do {                                                                        \
        auto&& ut_a = (actual);                                                 \
        auto&& ut_e = (expected);                                               \
        if (!(ut_a == ut_e))                                                    \
            ::ut::report_failure(__FILE__, __LINE__,                            \
                std::string("期望 ") + #actual + " == " + #expected +           \
                ", 实际为 " + ::ut::to_str(ut_a) + " / " + ::ut::to_str(ut_e)); \
    } while (0)

#define UT_CHECK_NE(actual, unexpected)                                         \
    do {                                                                        \
        auto&& ut_a = (actual);                                                 \
        auto&& ut_e = (unexpected);                                             \
        if (ut_a == ut_e)                                                       \
            ::ut::report_failure(__FILE__, __LINE__,                            \
                std::string("期望 ") + #actual + " != " + #unexpected +         \
                ", 但两者都是 " + ::ut::to_str(ut_a));                          \
    } while (0)

// 断言 haystack 中包含 needle (用于检查 ls 等命令的输出)
#define UT_CHECK_CONTAINS(haystack, needle)                                     \
    do {                                                                        \
        std::string ut_h = (haystack);                                          \
        std::string ut_n = (needle);                                            \
        if (ut_h.find(ut_n) == std::string::npos)                               \
            ::ut::report_failure(__FILE__, __LINE__,                            \
                std::string("输出中未找到 \"") + ut_n + "\", 实际输出: " + ut_h); \
    } while (0)

#define UT_CHECK_NOT_CONTAINS(haystack, needle)                                 \
    do {                                                                        \
        std::string ut_h = (haystack);                                          \
        std::string ut_n = (needle);                                            \
        if (ut_h.find(ut_n) != std::string::npos)                               \
            ::ut::report_failure(__FILE__, __LINE__,                            \
                std::string("输出中不应出现 \"") + ut_n + "\", 实际输出: " + ut_h); \
    } while (0)

namespace ut {

inline int run_all(bool full)
{
    int passed = 0, failed = 0;
    int known_reproduced = 0, known_fixed = 0, skipped = 0;
    std::vector<std::string> failed_names, fixed_names;

    std::string last_suite;
    for (std::size_t i = 0; i < registry().size(); i++)
    {
        TestCase& tc = registry()[i];

        if (tc.suite != last_suite)
        {
            std::cout << "\n=== " << tc.suite << " ===" << std::endl;
            last_suite = tc.suite;
        }

        if (tc.needs_full && !full)
        {
            std::cout << "  [SKIP ] " << tc.name << "   (需 --full 开启)" << std::endl;
            skipped++;
            continue;
        }

        current_failures().clear();
        tc.body();
        const bool ok = current_failures().empty();

        if (tc.known_bug)
        {
            if (ok)
            {
                std::cout << "  [FIXED] " << tc.name
                          << "   <- 缺陷已修复, 可从已知缺陷名单移出" << std::endl;
                known_fixed++;
                fixed_names.push_back(tc.suite + " / " + tc.name);
            }
            else
            {
                std::cout << "  [BUG  ] " << tc.name
                          << "   <- 已知缺陷复现 (预期)" << std::endl;
                known_reproduced++;
            }
        }
        else
        {
            if (ok)
            {
                std::cout << "  [PASS ] " << tc.name << std::endl;
                passed++;
            }
            else
            {
                std::cout << "  [FAIL ] " << tc.name << std::endl;
                for (std::size_t k = 0; k < current_failures().size(); k++)
                    std::cout << "           " << current_failures()[k] << std::endl;
                failed++;
                failed_names.push_back(tc.suite + " / " + tc.name);
            }
        }
    }

    std::cout << "\n----------------------------------------" << std::endl;
    std::cout << "通过 " << passed
              << "   失败 " << failed
              << "   已知缺陷复现 " << known_reproduced
              << "   已修复 " << known_fixed
              << "   跳过 " << skipped << std::endl;

    if (!fixed_names.empty())
    {
        std::cout << "\n以下已知缺陷用例已经通过, 请从 UT_TEST_KNOWN_BUG 改为 UT_TEST:"
                  << std::endl;
        for (std::size_t i = 0; i < fixed_names.size(); i++)
            std::cout << "  - " << fixed_names[i] << std::endl;
    }

    if (!failed_names.empty())
    {
        std::cout << "\n失败用例:" << std::endl;
        for (std::size_t i = 0; i < failed_names.size(); i++)
            std::cout << "  - " << failed_names[i] << std::endl;
    }

    // 退出码只由普通用例决定: 已知缺陷复现是当前代码的既有事实, 不算失败
    std::cout << (failed == 0 ? "\n结果: 通过\n" : "\n结果: 失败\n") << std::endl;
    return failed == 0 ? 0 : 1;
}

} // namespace ut
