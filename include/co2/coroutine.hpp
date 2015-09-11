/*//////////////////////////////////////////////////////////////////////////////
    Copyright (c) 2015 Jamboree

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//////////////////////////////////////////////////////////////////////////////*/
#ifndef CO2_COROUTINE_HPP_INCLUDED
#define CO2_COROUTINE_HPP_INCLUDED

#include <new>
#include <memory>
#include <utility>
#include <exception>
#include <boost/config.hpp>
#include <boost/assert.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/facilities/is_empty.hpp>
#include <boost/preprocessor/arithmetic/mod.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <co2/detail/void.hpp>

namespace co2
{
    template<class Promise = void>
    struct coroutine;

    template<class R>
    struct coroutine_traits
    {
        using promise_type = typename R::promise_type;
    };

    template<bool flag>
    struct suspend
    {
        bool await_ready() noexcept
        {
            return !flag;
        }

        void await_suspend(coroutine<> const&) noexcept {}

        void await_resume() noexcept {}
    };

    using suspend_always = suspend<true>;
    using suspend_never = suspend<false>;

    namespace detail
    {
        struct resumable_base
        {
            unsigned _next;
            unsigned _eh;
            virtual void run(coroutine<>&) = 0;
            virtual void unwind(coroutine<>&) = 0;
            virtual void release() noexcept = 0;
        };

        template<class Promise>
        struct resumable : resumable_base, private Promise
        {
            Promise& promise()
            {
                return *this;
            }

            static resumable* from_promise(Promise* p)
            {
                return static_cast<resumable*>(p);
            }
        };
    }

    template<>
    struct coroutine<void>
    {
        using handle_type = detail::resumable_base*;

        struct promise_type;

        coroutine() noexcept : _ptr() {}

        explicit coroutine(handle_type handle) noexcept : _ptr(handle) {}

        coroutine(coroutine&& other) noexcept : _ptr(other._ptr)
        {
            other._ptr = nullptr;
        }

        coroutine& operator=(coroutine other) noexcept
        {
            this->~coroutine();
            return *new(this) coroutine(std::move(other));
        }

        ~coroutine()
        {
            if (_ptr)
                _ptr->unwind(*this);
        }

        void reset() noexcept
        {
            if (_ptr)
            {
                _ptr->unwind(*this);
                _ptr = nullptr;
            }
        }

        void swap(coroutine& other) noexcept
        {
            std::swap(_ptr, other._ptr);
        }

        explicit operator bool() const noexcept
        {
            return !!_ptr;
        }

        void operator()() noexcept
        {
            _ptr->run(*this);
        }

        void resume()
        {
            _ptr->run(*this);
        }

        void* to_address() const noexcept
        {
            return _ptr;
        }

        handle_type detach() noexcept
        {
            auto handle = _ptr;
            _ptr = nullptr;
            return handle;
        }

    protected:

        detail::resumable_base* _ptr;
    };

    template<class Promise>
    struct coroutine : coroutine<>
    {
        using promise_type = Promise;

        coroutine() = default;

        coroutine(std::nullptr_t) noexcept {}

        explicit coroutine(detail::resumable<Promise>* p) noexcept : coroutine<>(p) {}

        explicit coroutine(Promise* p) noexcept
        {
            if (p)
                _ptr = detail::resumable<Promise>::from_promise(p);
        }

        Promise& promise() const noexcept
        {
            return static_cast<detail::resumable<Promise>*>(_ptr)->promise();
        }

        static void destroy(Promise* p)
        {
            detail::resumable<Promise>::from_promise(p)->release();
        }
    };

    struct coroutine<>::promise_type
    {
        bool initial_suspend() noexcept
        {
            return false;
        }

        bool final_suspend() noexcept
        {
            return false;
        }

        bool cancellation_requested() const noexcept
        {
            return false;
        }

        coroutine<promise_type> get_return_object(coroutine<promise_type>& coro)
        {
            coro.resume();
            return std::move(coro);
        }

        void set_result() noexcept {}
    };

    template<class Promise>
    inline bool operator==(coroutine<Promise> const& lhs, coroutine<Promise> const& rhs)
    {
        return lhs.to_address() == rhs.to_address();
    }

    template<class Promise>
    inline bool operator!=(coroutine<Promise> const& lhs, coroutine<Promise> const& rhs)
    {
        return lhs.to_address() != rhs.to_address();
    }
}

namespace co2 { namespace detail
{
    namespace temp
    {
        template<std::size_t Bytes, std::size_t RefSize = 1>
        using adjust_size = std::integral_constant<std::size_t, (Bytes > RefSize ? Bytes : RefSize)>;

        struct default_size
        {
            using _co2_sz = std::integral_constant<std::size_t, (sizeof(void*) + sizeof(int)) * 2>;
        };

        template<class T, bool NeedsAlloc>
        struct traits_non_ref
        {
            static void create(void* p, T&& t)
            {
                *static_cast<T**>(p) = new T(std::move(t));
            }

            static T& get(void* p)
            {
                return **static_cast<T**>(p);
            }

            static void reset(void* p)
            {
                delete *static_cast<T**>(p);
            }
        };

        template<class T>
        struct traits_non_ref<T, false>
        {
            static void create(void* p, T&& t)
            {
                new(p) T(std::move(t));
            }

            static T& get(void* p)
            {
                return *static_cast<T*>(p);
            }

            static void reset(void* p)
            {
                static_cast<T*>(p)->~T();
            }
        };

        template<class T>
        struct traits_ref
        {
            static void create(void* p, T& t)
            {
                *static_cast<T**>(p) = &t;
            }

            static T& get(void* p)
            {
                return **static_cast<T**>(p);
            }

            static void reset(void*) {}
        };

        template<class T, std::size_t Bytes>
        struct traits : traits_non_ref<T, (sizeof(T) > Bytes)> {};

        template<class T, std::size_t Bytes>
        struct traits<T&, Bytes> : traits_ref<T> {};

        template<class T, std::size_t Bytes>
        struct auto_reset
        {
            void* tmp;

            ~auto_reset()
            {
                traits<T, Bytes>::reset(tmp);
            }
        };
    }

    template<class T>
    using storage_for = std::aligned_storage_t<sizeof(T), alignof(T)>;

    struct exception_storage
    {
        void set(std::exception_ptr&& e) noexcept
        {
            new(&_data) std::exception_ptr(std::move(e));
        }

        std::exception_ptr get() noexcept
        {
            auto& ex = *reinterpret_cast<std::exception_ptr*>(&_data);
            std::exception_ptr ret(std::move(ex));
            ex.~exception_ptr();
            return ret;
        }

        storage_for<std::exception_ptr> _data;
    };

    using sentinel = std::integral_constant<unsigned, ~1u>;

    template<class Promise>
    inline auto resume(Promise* p) -> decltype(p->resume())
    {
        decltype(p->suspend()) is_also_required(void);
        return p->resume();
    }

    constexpr bool resume(void*)
    {
        return true;
    }

    template<class Promise>
    inline auto suspend(Promise* p) -> decltype(p->suspend())
    {
        decltype(p->resume()) is_also_required(void);
        p->suspend();
    }

    inline void suspend(void*) {}

    template<class Alloc, class T>
    using rebind_alloc_t =
        typename std::allocator_traits<Alloc>::template rebind_alloc<T>;

    template<class Promise, class F, class Alloc>
    struct frame final
      : resumable<Promise>
    {
        using alloc_t = rebind_alloc_t<Alloc, frame<Promise, F, Alloc>>;
        using traits = std::allocator_traits<alloc_t>;

        struct memory : alloc_t
        {
            explicit memory(alloc_t&& alloc) : alloc_t(std::move(alloc)) {}

            alignas(std::max_align_t) char tmp[F::_co2_sz::value];
            storage_for<F> f;
        } _mem;

        template<class Pack>
        static frame* create(alloc_t alloc, Pack&& pack)
        {
            auto p = traits::allocate(alloc, 1);
            try
            {
                return new(p) frame(alloc, std::forward<Pack>(pack));
            }
            catch (...)
            {
                traits::deallocate(alloc, p, 1);
                throw;
            }
        }

        template<class Pack>
        frame(alloc_t alloc, Pack&& pack) : _mem(std::move(alloc))
        {
            new(&_mem.f) F(std::forward<Pack>(pack));
            this->_next = F::_co2_start::value;
        }

        void run(coroutine<>& coro) override
        {
            if (detail::resume(&this->promise()))
            {
                reinterpret_cast<F&>(_mem.f)
                (static_cast<coroutine<Promise>&>(coro), this->_next, this->_eh, &_mem.tmp);
            }
            else
                coro.detach();
        }

        void unwind(coroutine<>& coro) override
        {
            if (detail::resume(&this->promise()))
            {
                reinterpret_cast<F&>(_mem.f)
                (static_cast<coroutine<Promise>&>(coro), ++this->_next, this->_eh, &_mem.tmp);
            }
        }

        void release() noexcept override
        {
            alloc_t alloc(static_cast<alloc_t&&>(_mem));
            this->~frame();
            traits::deallocate(alloc, this, 1);
        }
    };

    template<class T>
    using promise_t = typename T::promise_type;

    template<class T>
    T unrvref(T&&);

    struct avoid_plain_return
    {
        explicit avoid_plain_return() = default;
    };

    template<class Promise>
    inline auto final_result(Promise* p) -> decltype(p->set_result())
    {
        p->set_result();
    }

    inline void final_result(void*)
    {
        BOOST_ASSERT_MSG(false, "missing return statement");
    }

    template<class Promise, class T>
    inline void set_result(Promise& p, T&& t)
    {
        p.set_result(std::forward<T>(t));
    }

    template<class Promise>
    inline void set_result(Promise& p, void_)
    {
        p.set_result();
    }

    template<class Promise>
    inline auto set_exception(Promise* p, exception_storage& ex) -> decltype(p->set_exception(ex.get()))
    {
        return p->set_exception(ex.get());
    }

    inline void set_exception(void*, exception_storage& ex)
    {
        std::rethrow_exception(ex.get());
    }

    template<class Promise>
    inline auto cancel(Promise* p) -> decltype(p->cancel())
    {
        return p->cancel();
    }

    inline void cancel(void*) {}

    template<class... T>
    inline std::allocator<void> get_alloc(T&&...)
    {
        return {};
    }

    template<class A, class... T>
    inline A get_alloc(std::allocator_arg_t, A a, T&&...)
    {
        return a;
    }

    template<class F, class P>
    struct finalizer
    {
        F* f;
        coroutine<P>& coro;
        P& promise;

        ~finalizer()
        {
            f->~F();
            coro.detach(); // this order is important!
            if (!promise.final_suspend())
                coroutine<P>::destroy(&promise);
        }
    };

    template<class T>
    inline auto await_ready(T& t) -> decltype(t.await_ready())
    {
        return t.await_ready();
    }

    template<class T, class F>
    inline auto await_suspend(T& t, F&& f) ->
        decltype(t.await_suspend(std::forward<F>(f)))
    {
        return t.await_suspend(std::forward<F>(f));
    }

    template<class T>
    inline auto await_resume(T& t) -> decltype(t.await_resume())
    {
        return t.await_resume();
    }

    struct await_ready_fn
    {
        template<class T>
        auto operator()(T& t) const -> decltype(await_ready(t))
        {
            return await_ready(t);
        }
    };

    struct await_suspend_fn
    {
        template<class T, class F>
        auto operator()(T& t, F&& f) const ->
            decltype(await_suspend(t, std::forward<F>(f)))
        {
            return await_suspend(t, std::forward<F>(f));
        }
    };

    struct await_resume_fn
    {
        template<class T>
        auto operator()(T& t) const -> decltype(await_resume(t))
        {
            return await_resume(t);
        }
    };
}}

namespace co2
{
    constexpr detail::await_ready_fn await_ready{};
    constexpr detail::await_suspend_fn await_suspend{};
    constexpr detail::await_resume_fn await_resume{};

    template<class T>
    struct await_result
    {
        using type = decltype(await_resume(std::declval<std::add_lvalue_reference_t<T>>()));
    };

    template<class T>
    using await_result_t = decltype(await_resume(std::declval<std::add_lvalue_reference_t<T>>()));

    namespace detail
    {
        template<class Task>
        struct ready_awaiter
        {
            Task task;

            bool await_ready()
            {
                return co2::await_ready(task);
            }

            template<class F>
            auto await_suspend(F&& f) -> decltype(co2::await_suspend(task, std::forward<F>(f)))
            {
                return co2::await_suspend(task, std::forward<F>(f));
            }

            void await_resume() noexcept {}
        };
    }

    template<class Task>
    inline detail::ready_awaiter<Task> ready(Task&& task)
    {
        return {std::forward<Task>(task)};
    }
}

#   if defined(BOOST_MSVC)
#   define _impl_CO2_IS_EMPTY BOOST_PP_IS_EMPTY
#   define _impl_CO2_PUSH_NAME_HIDDEN_WARNING                                   \
    __pragma(warning(push))                                                     \
    __pragma(warning(disable:4456))                                             \
    /***/
#   define _impl_CO2_POP_WARNING __pragma(warning(pop))
#   else
#   define _impl_CO2_PUSH_NAME_HIDDEN_WARNING
#   define _impl_CO2_POP_WARNING
// The IS_EMPTY trick is from:
// http://gustedt.wordpress.com/2010/06/08/detect-empty-macro-arguments/
// IS_EMPTY {
#   define _impl_CO2_ARG16(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, ...) _15
#   define _impl_CO2_HAS_COMMA(...) _impl_CO2_ARG16(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#   define _impl_CO2_TRIGGER_PARENTHESIS_(...) ,

#   define _impl_CO2_IS_EMPTY(...)                                              \
    _impl_CO2_IS_EMPTY_IMPL(                                                    \
        /* test if there is just one argument, eventually an empty one */       \
        _impl_CO2_HAS_COMMA(__VA_ARGS__),                                       \
        /* test if _impl_CO2_TRIGGER_PARENTHESIS_ together with the argument adds a comma */\
        _impl_CO2_HAS_COMMA(_impl_CO2_TRIGGER_PARENTHESIS_ __VA_ARGS__),        \
        /* test if the argument together with a parenthesis adds a comma */     \
        _impl_CO2_HAS_COMMA(__VA_ARGS__ (/*empty*/)),                           \
        /* test if placing it between _impl_CO2_TRIGGER_PARENTHESIS_ and the parenthesis adds a comma */\
        _impl_CO2_HAS_COMMA(_impl_CO2_TRIGGER_PARENTHESIS_ __VA_ARGS__ (/*empty*/))\
    )                                                                           \
    /***/

#   define _impl_CO2_PASTE5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#   define _impl_CO2_IS_EMPTY_IMPL(_0, _1, _2, _3) _impl_CO2_HAS_COMMA(_impl_CO2_PASTE5(_impl_CO2_IS_EMPTY_CASE_, _0, _1, _2, _3))
#   define _impl_CO2_IS_EMPTY_CASE_0001 ,
    // } IS_EMPTY
#   endif

#define _impl_CO2_TUPLE_FOR_EACH_IMPL(macro, t)                                 \
BOOST_PP_SEQ_FOR_EACH(macro, ~, BOOST_PP_VARIADIC_TO_SEQ t)                     \
/***/

#define _impl_CO2_TUPLE_FOR_EACH_EMPTY(macro, t)

#define _impl_CO2_TUPLE_FOR_EACH(macro, t)                                      \
BOOST_PP_IF(_impl_CO2_IS_EMPTY t, _impl_CO2_TUPLE_FOR_EACH_EMPTY,               \
    _impl_CO2_TUPLE_FOR_EACH_IMPL)(macro, t)                                    \
/***/

#define _impl_CO2_AWAIT(ret, expr, next)                                        \
do {                                                                            \
    using _co2_expr_t = decltype(::co2::detail::unrvref(expr));                 \
    using _co2_await = ::co2::detail::temp::traits<_co2_expr_t, _co2_sz::value>;\
    _co2_await::create(_co2_tmp, expr);                                         \
    try                                                                         \
    {                                                                           \
        if (!::co2::await_ready(_co2_await::get(_co2_tmp)))                     \
        {                                                                       \
            _co2_next = next;                                                   \
            ::co2::detail::suspend(&_co2_p);                                    \
            if ((::co2::await_suspend(_co2_await::get(_co2_tmp), _co2_c),       \
                ::co2::detail::void_{}) || !::co2::detail::resume(&_co2_p))     \
                return ::co2::detail::avoid_plain_return{};                     \
        }                                                                       \
    }                                                                           \
    catch (...)                                                                 \
    {                                                                           \
        _co2_await::reset(_co2_tmp);                                            \
        throw;                                                                  \
    }                                                                           \
    case next:                                                                  \
    if (_co2_p.cancellation_requested())                                        \
    {                                                                           \
        case __COUNTER__:                                                       \
        _co2_await::reset(_co2_tmp);                                            \
        ::co2::detail::cancel(&_co2_p);                                         \
        goto _co2_finalize;                                                     \
    }                                                                           \
    ::co2::detail::temp::auto_reset<_co2_expr_t, _co2_sz::value>                \
        _co2_reset = {_co2_tmp};                                                \
    ret (::co2::await_resume(_co2_await::get(_co2_tmp)));                       \
} while (false)                                                                 \
/***/

#define _impl_CO2_SUSPEND(expr, next)                                           \
{                                                                               \
    if (_co2_p.expr)                                                            \
    {                                                                           \
        _co2_next = next;                                                       \
        ::co2::detail::suspend(&_co2_p);                                        \
        return ::co2::detail::avoid_plain_return{};                             \
    }                                                                           \
    case next:                                                                  \
    if (_co2_p.cancellation_requested())                                        \
    {                                                                           \
        case __COUNTER__:                                                       \
        ::co2::detail::cancel(&_co2_p);                                         \
        goto _co2_finalize;                                                     \
    }                                                                           \
}                                                                               \
/***/

#define CO2_AWAIT_APPLY(f, expr) _impl_CO2_AWAIT(f, expr, __COUNTER__)
#define CO2_AWAIT_SET(var, expr) _impl_CO2_AWAIT(var =, expr, __COUNTER__)
#define CO2_AWAIT(expr) _impl_CO2_AWAIT(, expr, __COUNTER__)
#define CO2_AWAIT_LET(let, expr, ...)                                           \
_impl_CO2_AWAIT(([this](let) __VA_ARGS__), expr, __COUNTER__)                   \
/***/

#define CO2_YIELD(...) CO2_AWAIT(_co2_p.yield_value(__VA_ARGS__))

#define CO2_RETURN(...)                                                         \
do {                                                                            \
    _co2_next = ::co2::detail::sentinel::value;                                 \
    _co2_p.set_result(__VA_ARGS__);                                             \
    goto _co2_finalize;                                                         \
} while (false)                                                                 \
/***/

#define CO2_RETURN_LOCAL(var)                                                   \
do {                                                                            \
    _co2_next = ::co2::detail::sentinel::value;                                 \
    _co2_p.set_result(std::forward<decltype(var)>(var));                        \
    goto _co2_finalize;                                                         \
} while (false)                                                                 \
/***/

#define CO2_RETURN_FROM(...)                                                    \
do {                                                                            \
    _co2_next = ::co2::detail::sentinel::value;                                 \
    ::co2::detail::set_result(_co2_p, (__VA_ARGS__, ::co2::detail::void_{}));   \
    goto _co2_finalize;                                                         \
} while (false)                                                                 \
/***/

#define CO2_AWAIT_RETURN(expr) _impl_CO2_AWAIT(CO2_RETURN_FROM, expr, __COUNTER__)

#define CO2_TRY                                                                 \
_impl_CO2_PUSH_NAME_HIDDEN_WARNING                                              \
using _co2_prev_eh = _co2_curr_eh;                                              \
using _co2_curr_eh = std::integral_constant<unsigned, __COUNTER__>;             \
_impl_CO2_POP_WARNING                                                           \
_co2_eh = _co2_curr_eh::value;                                                  \
if (true)                                                                       \
/***/

#define CO2_CATCH                                                               \
else case _co2_curr_eh::value:                                                  \
    try                                                                         \
    {                                                                           \
        _co2_eh = _co2_prev_eh::value;                                          \
        std::rethrow_exception(_co2_ex.get());                                  \
    }                                                                           \
    catch                                                                       \
/***/

#define _impl_CO2_SWITCH_LABEL(n) BOOST_PP_CAT(BOOST_PP_CAT(_co2_case_, __LINE__), n)

#define _impl_CO2_SWITCH_CASE(r, _, i, e)                                       \
BOOST_PP_IF(BOOST_PP_MOD(i, 2), , e: goto _impl_CO2_SWITCH_LABEL(i);)           \
/***/

#define _impl_CO2_UNPAREN(...) __VA_ARGS__
#define _impl_CO2_SWITCH_BODY_TRUE(i, e) _impl_CO2_SWITCH_LABEL(BOOST_PP_DEC(i)): _impl_CO2_UNPAREN e
#define _impl_CO2_SWITCH_BODY_FALSE(i, e)

#define _impl_CO2_SWITCH_BODY(r, _, i, e)                                       \
BOOST_PP_IF(BOOST_PP_MOD(i, 2),                                                 \
    _impl_CO2_SWITCH_BODY_TRUE, _impl_CO2_SWITCH_BODY_FALSE)(i, e)              \
/***/

#define _impl_CO2_SWITCH(n, seq)                                                \
switch (n)                                                                      \
{                                                                               \
    BOOST_PP_SEQ_FOR_EACH_I(_impl_CO2_SWITCH_CASE, ~, seq)                      \
}                                                                               \
while (false)                                                                   \
{                                                                               \
    BOOST_PP_SEQ_FOR_EACH_I(_impl_CO2_SWITCH_BODY, ~, seq)                      \
        break;                                                                  \
}                                                                               \
/***/

#define CO2_SWITCH(n, ...) _impl_CO2_SWITCH(n, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#if defined(BOOST_GCC)
#define _impl_CO2_TYPE_PARAM(r, _, e) using BOOST_PP_CAT(e, _t) = decltype(e);
#define _impl_CO2_DECL_PARAM(r, _, e) BOOST_PP_CAT(e, _t) e;
#define _impl_CO2_K(args)                                                       \
struct _co2_KK                                                                  \
{                                                                               \
    _impl_CO2_TUPLE_FOR_EACH(_impl_CO2_TYPE_PARAM, args)                        \
    struct pack                                                                 \
    {                                                                           \
        _impl_CO2_TUPLE_FOR_EACH(_impl_CO2_DECL_PARAM, args)                    \
    };                                                                          \
};                                                                              \
using _co2_K = typename _co2_KK::pack;                                          \
/***/
#else
#define _impl_CO2_DECL_PARAM(r, _, e) decltype(e) e;
#define _impl_CO2_K(args)                                                       \
struct _co2_K                                                                   \
{                                                                               \
    _impl_CO2_TUPLE_FOR_EACH(_impl_CO2_DECL_PARAM, args)                        \
};                                                                              \
/***/
#endif

#define _impl_CO2_FWD_PARAM(r, _, e) std::forward<decltype(e)>(e),
#define _impl_CO2_USE_PARAM(r, _, e) using _co2_K::e;

#define _impl_CO2_1ST(a, b) a
#define _impl_CO2_2ND(a, b) b

#define _impl_CO2_NEW_ALLOC(alloc, args) std::forward<decltype(alloc)>(alloc)
#define _impl_CO2_OLD_ALLOC(alloc, args)                                        \
::co2::detail::get_alloc(_impl_CO2_TUPLE_FOR_EACH(                              \
    _impl_CO2_FWD_PARAM, args) ::co2::detail::void_{})                          \
/***/

#define _impl_CO2_INVOKE(f, args) f args
#define _impl_CO2_DISPATCH_impl_CO2_GET_ALLOC_ (_impl_CO2_OLD_ALLOC, ~)
#define _impl_CO2_DISPATCH_NEW_ALLOC
#define _impl_CO2_GET_ALLOC_new(a) _NEW_ALLOC (_impl_CO2_NEW_ALLOC, a)
#define _impl_CO2_SKIP_CAPTURE(...) 
#define _impl_CO2_GET_ALLOC(x) BOOST_PP_EXPAND(_impl_CO2_INVOKE(                \
    BOOST_PP_CAT, (_impl_CO2_DISPATCH, _impl_CO2_INVOKE(                        \
        BOOST_PP_CAT, (_impl_CO2_GET_ALLOC_, _impl_CO2_SKIP_CAPTURE x)))))      \
/***/

#define _impl_CO2_SEPARATE_ALLOC(...) (__VA_ARGS__),
#define _impl_CO2_GET_ARGS(x) BOOST_PP_EXPAND(_impl_CO2_INVOKE(                 \
    _impl_CO2_1ST, (_impl_CO2_SEPARATE_ALLOC x)))                               \
/***/

#define CO2_TEMP_SIZE(bytes) using _co2_sz = ::co2::detail::temp::adjust_size<bytes>

#define _impl_CO2_HEAD(R, args, alloc, ...)                                     \
{                                                                               \
    using _co2_T = ::co2::coroutine_traits<R>;                                  \
    using _co2_P = ::co2::detail::promise_t<_co2_T>;                            \
    using _co2_C = ::co2::coroutine<_co2_P>;                                    \
    _impl_CO2_K(args)                                                           \
    _co2_K _co2_k = {_impl_CO2_TUPLE_FOR_EACH(_impl_CO2_FWD_PARAM, args)};      \
    auto _co2_a(_impl_CO2_1ST alloc(_impl_CO2_2ND alloc, args));                \
    struct _co2_F : ::co2::detail::temp::default_size, _co2_K                   \
    {                                                                           \
        _impl_CO2_TUPLE_FOR_EACH(_impl_CO2_USE_PARAM, args)                     \
        __VA_ARGS__                                                             \
        _co2_F(_co2_K&& pack) : _co2_K(std::move(pack)) {}                      \
        using _co2_start = std::integral_constant<unsigned, __COUNTER__>;       \
        ::co2::detail::avoid_plain_return operator()                            \
        (_co2_C& _co2_c, unsigned& _co2_next, unsigned& _co2_eh, void* _co2_tmp)\
        {                                                                       \
            (void)_co2_tmp;                                                     \
            auto& _co2_p = _co2_c.promise();                                    \
            ::co2::detail::exception_storage _co2_ex;                           \
            _co2_try_again:                                                     \
            try                                                                 \
            {                                                                   \
                switch (_co2_next)                                              \
                {                                                               \
                case _co2_start::value:                                         \
                    using _co2_curr_eh = ::co2::detail::sentinel;               \
                    _co2_eh = _co2_curr_eh::value;                              \
                    _impl_CO2_SUSPEND(initial_suspend(), __COUNTER__);          \
/***/

#define CO2_BEG(R, capture, ...) -> R _impl_CO2_HEAD(R,                         \
    _impl_CO2_GET_ARGS(capture), _impl_CO2_GET_ALLOC(capture), __VA_ARGS__)     \
/***/

#define CO2_END                                                                 \
                    ::co2::detail::final_result(&_co2_p);                       \
                _co2_finalize:                                                  \
                    ::co2::detail::finalizer<_co2_F, _co2_P>{this, _co2_c, _co2_p};\
                }                                                               \
            }                                                                   \
            catch (...)                                                         \
            {                                                                   \
                _co2_next = _co2_eh;                                            \
                _co2_ex.set(std::current_exception());                          \
                if (_co2_next != ::co2::detail::sentinel::value)                \
                    goto _co2_try_again;                                        \
                ::co2::detail::finalizer<_co2_F, _co2_P> fin{this, _co2_c, _co2_p};\
                ::co2::detail::set_exception(&_co2_p, _co2_ex);                 \
            }                                                                   \
            return ::co2::detail::avoid_plain_return{};                         \
        }                                                                       \
    };                                                                          \
    using _co2_FR = ::co2::detail::frame<_co2_P, _co2_F, decltype(_co2_a)>;     \
    _co2_C _co2_c(_co2_FR::create(std::move(_co2_a), std::move(_co2_k)));       \
    return _co2_c.promise().get_return_object(_co2_c);                          \
}                                                                               \
/***/

#endif