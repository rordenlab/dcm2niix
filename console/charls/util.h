//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//


#ifndef CHARLS_UTIL
#define CHARLS_UTIL

#include "publictypes.h"
#include <vector>
#include <system_error>
#include <memory>

// ReSharper disable once CppUnusedIncludeDirective
#include <cassert>

// Use an uppercase alias for assert to make it clear that it is a pre-processor macro.
#define ASSERT(t) assert(t)

//https://github.com/team-charls/charls/issues/38
#if __cplusplus == 201103L
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif

// Only use __forceinline for the Microsoft C++ compiler in release mode (verified scenario)
// Use the built-in optimizer for all other C++ compilers.
// Note: usage of FORCE_INLINE may be reduced in the future as the latest generation of C++ compilers
// can handle optimization by themselves.
#ifndef FORCE_INLINE
#  ifdef _MSC_VER
#    ifdef NDEBUG
#      define FORCE_INLINE __forceinline
#    else
#      define FORCE_INLINE
#    endif
#  else
#    define FORCE_INLINE
#  endif
#endif

#ifdef _MSC_VER
#define WARNING_SUPPRESS(x) __pragma(warning(push)) __pragma(warning(disable : x))
#define WARNING_UNSUPPRESS() __pragma(warning(pop))
#else
#define WARNING_SUPPRESS(x)
#define WARNING_UNSUPPRESS()
#endif


constexpr size_t int32_t_bit_count = sizeof(int32_t) * 8;


inline void push_back(std::vector<uint8_t>& values, uint16_t value)
{
    values.push_back(uint8_t(value / 0x100));
    values.push_back(uint8_t(value % 0x100));
}


inline int32_t log_2(int32_t n) noexcept
{
    int32_t x = 0;
    while (n > (static_cast<int32_t>(1) << x))
    {
        ++x;
    }
    return x;
}


inline int32_t Sign(int32_t n) noexcept
{
    return (n >> (int32_t_bit_count - 1)) | 1;
}


inline int32_t BitWiseSign(int32_t i) noexcept
{
    return i >> (int32_t_bit_count - 1);
}


template<typename T>
struct Triplet
{
    Triplet() noexcept :
        v1(0),
        v2(0),
        v3(0)
    {}

    Triplet(int32_t x1, int32_t x2, int32_t x3) noexcept :
        v1(static_cast<T>(x1)),
        v2(static_cast<T>(x2)),
        v3(static_cast<T>(x3))
    {}

    union
    {
        T v1;
        T R;
    };
    union
    {
        T v2;
        T G;
    };
    union
    {
        T v3;
        T B;
    };
};


inline bool operator==(const Triplet<uint8_t>& lhs, const Triplet<uint8_t>& rhs) noexcept
{
    return lhs.v1 == rhs.v1 && lhs.v2 == rhs.v2 && lhs.v3 == rhs.v3;
}


inline bool operator!=(const Triplet<uint8_t>& lhs, const Triplet<uint8_t>& rhs) noexcept
{
    return !(lhs == rhs);
}


template<typename sample>
struct Quad : Triplet<sample>
{
    Quad() :
        v4(0)
        {}

    WARNING_SUPPRESS(26495) // false warning that v4 is uninitialized
    Quad(Triplet<sample> triplet, int32_t alpha) noexcept
            :
        Triplet<sample>(triplet),
        A(static_cast<sample>(alpha))
        {}
    WARNING_UNSUPPRESS()

    union
    {
        sample v4;
        sample A;
    };
};


template<int size>
struct FromBigEndian
{
};


template<>
struct FromBigEndian<4>
{
    FORCE_INLINE static unsigned int Read(const uint8_t* pbyte) noexcept
    {
        return (pbyte[0] << 24u) + (pbyte[1] << 16u) + (pbyte[2] << 8u) + (pbyte[3] << 0u);
    }
};


template<>
struct FromBigEndian<8>
{
    FORCE_INLINE static uint64_t Read(const uint8_t* pbyte) noexcept
    {
        return (static_cast<uint64_t>(pbyte[0]) << 56u) + (static_cast<uint64_t>(pbyte[1]) << 48u) +
               (static_cast<uint64_t>(pbyte[2]) << 40u) + (static_cast<uint64_t>(pbyte[3]) << 32u) +
               (static_cast<uint64_t>(pbyte[4]) << 24u) + (static_cast<uint64_t>(pbyte[5]) << 16u) +
               (static_cast<uint64_t>(pbyte[6]) <<  8u) + (static_cast<uint64_t>(pbyte[7]) << 0u);
    }
};


class charls_error : public std::system_error
{
public:
    explicit charls_error(charls::ApiResult errorCode)
        : system_error(static_cast<int>(errorCode), CharLSCategoryInstance())
    {
    }


    charls_error(charls::ApiResult errorCode, const std::string& message)
        : system_error(static_cast<int>(errorCode), CharLSCategoryInstance(), message)
    {
    }

private:
    static const std::error_category& CharLSCategoryInstance() noexcept;
};


inline void SkipBytes(ByteStreamInfo& streamInfo, std::size_t count) noexcept
{
    if (!streamInfo.rawData)
        return;

    streamInfo.rawData += count;
    streamInfo.count -= count;
}


template<typename T>
std::ostream& operator<<(typename std::enable_if<std::is_enum<T>::value, std::ostream>::type& stream, const T& e)
{
    return stream << static_cast<typename std::underlying_type<T>::type>(e);
}

#endif
