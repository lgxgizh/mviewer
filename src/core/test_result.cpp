#include "domain/Result.h"

#include <iostream>
#include <string>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << std::endl;                                             \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << std::endl;                                             \
        }                                                                                          \
    } while (0)

using namespace mviewer::domain;

int main()
{
    // 1. Basic value success
    {
        Result<int, std::string> res = 42;
        CHECK(res.has_value(), "Result<int> has value");
        CHECK(static_cast<bool>(res), "Result<int> bool operator");
        CHECK(res.value() == 42, "res.value() == 42");
        CHECK(*res == 42, "*res == 42");
        CHECK(res.value_or(100) == 42, "res.value_or(100) == 42");
    }

    // 2. Factory ok
    {
        auto res = Result<std::string>::ok("success");
        CHECK(res.has_value(), "Result::ok has value");
        CHECK(res.value() == "success", "res.value() == 'success'");
    }

    // 3. Basic error
    {
        auto res = Result<int>::err("failure reason");
        CHECK(!res.has_value(), "Result::err has no value");
        CHECK(!static_cast<bool>(res), "Result::err bool is false");
        CHECK(res.error() == "failure reason", "res.error() matches");
        CHECK(res.value_or(99) == 99, "res.value_or fallback works");
    }

    // 4. Monadic map
    {
        Result<int> r1 = 10;
        auto r2 = r1.map([](int x) { return x * 2; });
        CHECK(r2.has_value() && r2.value() == 20, "r1.map doubles value");

        Result<int> r3 = makeUnexpected(std::string("initial error"));
        auto r4 = r3.map([](int x) { return x * 2; });
        CHECK(!r4.has_value(), "r3.map propagates error");
        CHECK(r4.error() == "initial error", "r4 error preserved");
    }

    // 5. Monadic and_then
    {
        auto divide = [](int a, int b) -> Result<int> {
            if (b == 0)
                return Result<int>::err("division by zero");
            return Result<int>::ok(a / b);
        };

        Result<int> valid = 100;
        auto step1 = valid.and_then([&](int v) { return divide(v, 2); });
        CHECK(step1.has_value() && step1.value() == 50, "and_then 100 / 2 = 50");

        auto step2 = step1.and_then([&](int v) { return divide(v, 0); });
        CHECK(!step2.has_value(), "and_then division by zero fails");
        CHECK(step2.error() == "division by zero", "and_then error preserved");

        auto step3 = step2.and_then([&](int v) { return divide(v, 5); });
        CHECK(!step3.has_value(), "and_then skips after failure");
        CHECK(step3.error() == "division by zero", "step3 error unchanged");
    }

    // 6. Monadic map_error
    {
        Result<int, int> r1 = makeUnexpected(404);
        auto r2 = r1.map_error([](int code) { return "Error code " + std::to_string(code); });
        CHECK(!r2.has_value(), "map_error remains error");
        CHECK(r2.error() == "Error code 404", "map_error transformed error");
    }

    // 7. Void specialization
    {
        Result<void> v1 = Result<void>::ok();
        CHECK(v1.has_value(), "Result<void> ok has value");

        Result<void> v2 = Result<void>::err("void failure");
        CHECK(!v2.has_value(), "Result<void> err has no value");
        CHECK(v2.error() == "void failure", "Result<void> error matches");
    }

    // 8. DecodeError domain type
    {
        DecodeError err{DecodeErrorCode::FileNotFound, "file missing", "/path/to/img.png"};
        CHECK(err.code == DecodeErrorCode::FileNotFound, "err.code is FileNotFound");
        std::string s = err.toString();
        CHECK(s.find("file missing") != std::string::npos, "err.toString() contains message");
        CHECK(s.find("/path/to/img.png") != std::string::npos, "err.toString() contains path");

        Result<int, DecodeError> decodeRes = Result<int, DecodeError>::err(err);
        CHECK(!decodeRes.has_value(), "decodeRes is error");
        CHECK(decodeRes.error().code == DecodeErrorCode::FileNotFound, "decodeRes has error code");
    }

    std::cout << "All Result<T, E> tests passed!" << std::endl;
    return 0;
}
