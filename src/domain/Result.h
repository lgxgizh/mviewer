#pragma once

#include <cassert>
#include <exception>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace mviewer::domain
{

template <typename E>
class Unexpected
{
  public:
    constexpr explicit Unexpected(const E &e) : m_err(e) {}
    constexpr explicit Unexpected(E &&e) : m_err(std::move(e)) {}

    constexpr const E &error() const & noexcept { return m_err; }
    constexpr E &error() & noexcept { return m_err; }
    constexpr E &&error() && noexcept { return std::move(m_err); }

  private:
    E m_err;
};

template <typename E>
constexpr Unexpected<std::decay_t<E>> makeUnexpected(E &&e)
{
    return Unexpected<std::decay_t<E>>(std::forward<E>(e));
}

// Lightweight monadic Result container modeled after C++23 std::expected.
// Domain layer: zero dependencies, pure C++20 standard library.
template <typename T, typename E = std::string>
class Result
{
  public:
    using value_type = T;
    using error_type = E;

    constexpr Result(const T &val) : m_storage(std::in_place_index<0>, val) {}
    constexpr Result(T &&val) : m_storage(std::in_place_index<0>, std::move(val)) {}

    template <typename Err>
        requires std::is_constructible_v<E, Err>
    constexpr Result(const Unexpected<Err> &unexp)
        : m_storage(std::in_place_index<1>, unexp.error())
    {
    }

    template <typename Err>
        requires std::is_constructible_v<E, Err>
    constexpr Result(Unexpected<Err> &&unexp)
        : m_storage(std::in_place_index<1>, std::move(unexp).error())
    {
    }

    static Result ok(T val) { return Result(std::move(val)); }
    static Result err(E err) { return Result(makeUnexpected(std::move(err))); }

    constexpr bool has_value() const noexcept { return m_storage.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr const T &value() const &
    {
        if (!has_value())
            throw std::bad_variant_access();
        return std::get<0>(m_storage);
    }

    constexpr T &value() &
    {
        if (!has_value())
            throw std::bad_variant_access();
        return std::get<0>(m_storage);
    }

    constexpr T &&value() &&
    {
        if (!has_value())
            throw std::bad_variant_access();
        return std::move(std::get<0>(m_storage));
    }

    constexpr const T &operator*() const & noexcept
    {
        assert(has_value());
        return std::get<0>(m_storage);
    }

    constexpr T &operator*() & noexcept
    {
        assert(has_value());
        return std::get<0>(m_storage);
    }

    constexpr const T &&operator*() const && noexcept
    {
        assert(has_value());
        return std::move(std::get<0>(m_storage));
    }

    constexpr T &&operator*() && noexcept
    {
        assert(has_value());
        return std::move(std::get<0>(m_storage));
    }

    constexpr const T *operator->() const noexcept
    {
        assert(has_value());
        return &std::get<0>(m_storage);
    }

    constexpr T *operator->() noexcept
    {
        assert(has_value());
        return &std::get<0>(m_storage);
    }

    template <typename U>
    constexpr T value_or(U &&default_value) const &
    {
        return has_value() ? std::get<0>(m_storage)
                           : static_cast<T>(std::forward<U>(default_value));
    }

    template <typename U>
    constexpr T value_or(U &&default_value) &&
    {
        return has_value() ? std::move(std::get<0>(m_storage))
                           : static_cast<T>(std::forward<U>(default_value));
    }

    constexpr const E &error() const &
    {
        if (has_value())
            throw std::bad_variant_access();
        return std::get<1>(m_storage);
    }

    constexpr E &error() &
    {
        if (has_value())
            throw std::bad_variant_access();
        return std::get<1>(m_storage);
    }

    constexpr E &&error() &&
    {
        if (has_value())
            throw std::bad_variant_access();
        return std::move(std::get<1>(m_storage));
    }

    // Monadic map: transforms value if ok, propagates error if failed (const &)
    template <typename F>
    auto map(F &&f) const &
    {
        using ReturnType = std::invoke_result_t<F, const T &>;
        if (has_value())
        {
            if constexpr (std::is_void_v<ReturnType>)
            {
                std::invoke(std::forward<F>(f), value());
                return Result<void, E>::ok();
            }
            else
            {
                return Result<ReturnType, E>::ok(std::invoke(std::forward<F>(f), value()));
            }
        }
        return Result<ReturnType, E>::err(error());
    }

    // Monadic map: transforms value if ok, propagates error if failed (&&)
    template <typename F>
    auto map(F &&f) &&
    {
        using ReturnType = std::invoke_result_t<F, T &&>;
        if (has_value())
        {
            if constexpr (std::is_void_v<ReturnType>)
            {
                std::invoke(std::forward<F>(f), std::move(value()));
                return Result<void, E>::ok();
            }
            else
            {
                return Result<ReturnType, E>::ok(
                    std::invoke(std::forward<F>(f), std::move(value())));
            }
        }
        return Result<ReturnType, E>::err(std::move(error()));
    }

    // Monadic and_then: chains with another Result returning function (const &)
    template <typename F>
    auto and_then(F &&f) const &
    {
        using ResultType = std::invoke_result_t<F, const T &>;
        if (has_value())
        {
            return std::invoke(std::forward<F>(f), value());
        }
        return ResultType::err(error());
    }

    // Monadic and_then: chains with another Result returning function (&&)
    template <typename F>
    auto and_then(F &&f) &&
    {
        using ResultType = std::invoke_result_t<F, T &&>;
        if (has_value())
        {
            return std::invoke(std::forward<F>(f), std::move(value()));
        }
        return ResultType::err(std::move(error()));
    }

    // Monadic map_error: transforms error if failed, propagates value if ok (const &)
    template <typename F>
    auto map_error(F &&f) const &
    {
        using NewError = std::invoke_result_t<F, const E &>;
        if (has_value())
        {
            return Result<T, NewError>::ok(value());
        }
        return Result<T, NewError>::err(std::invoke(std::forward<F>(f), error()));
    }

    // Monadic map_error: transforms error if failed, propagates value if ok (&&)
    template <typename F>
    auto map_error(F &&f) &&
    {
        using NewError = std::invoke_result_t<F, E &&>;
        if (has_value())
        {
            return Result<T, NewError>::ok(std::move(value()));
        }
        return Result<T, NewError>::err(std::invoke(std::forward<F>(f), std::move(error())));
    }

  private:
    std::variant<T, E> m_storage;
};

// Void specialization for operations that return status without a payload value
template <typename E>
class Result<void, E>
{
  public:
    using value_type = void;
    using error_type = E;

    constexpr Result() : m_has_value(true) {}

    template <typename Err>
        requires std::is_constructible_v<E, Err>
    constexpr Result(const Unexpected<Err> &unexp)
        : m_has_value(false), m_error(unexp.error())
    {
    }

    template <typename Err>
        requires std::is_constructible_v<E, Err>
    constexpr Result(Unexpected<Err> &&unexp)
        : m_has_value(false), m_error(std::move(unexp).error())
    {
    }

    static Result ok() { return Result(); }
    static Result err(E err) { return Result(makeUnexpected(std::move(err))); }

    constexpr bool has_value() const noexcept { return m_has_value; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr const E &error() const &
    {
        if (m_has_value)
            throw std::bad_variant_access();
        return m_error;
    }

    constexpr E &error() &
    {
        if (m_has_value)
            throw std::bad_variant_access();
        return m_error;
    }

    constexpr E &&error() &&
    {
        if (m_has_value)
            throw std::bad_variant_access();
        return std::move(m_error);
    }

    template <typename F>
    auto and_then(F &&f) const &
    {
        using ResultType = std::invoke_result_t<F>;
        if (has_value())
        {
            return std::invoke(std::forward<F>(f));
        }
        return ResultType::err(error());
    }

    template <typename F>
    auto and_then(F &&f) &&
    {
        using ResultType = std::invoke_result_t<F>;
        if (has_value())
        {
            return std::invoke(std::forward<F>(f));
        }
        return ResultType::err(std::move(error()));
    }

    template <typename F>
    auto map_error(F &&f) const &
    {
        using NewError = std::invoke_result_t<F, const E &>;
        if (has_value())
        {
            return Result<void, NewError>::ok();
        }
        return Result<void, NewError>::err(std::invoke(std::forward<F>(f), error()));
    }

    template <typename F>
    auto map_error(F &&f) &&
    {
        using NewError = std::invoke_result_t<F, E &&>;
        if (has_value())
        {
            return Result<void, NewError>::ok();
        }
        return Result<void, NewError>::err(std::invoke(std::forward<F>(f), std::move(error())));
    }

  private:
    bool m_has_value = true;
    E m_error{};
};

} // namespace mviewer::domain
