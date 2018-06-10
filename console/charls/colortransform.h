//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//

#ifndef CHARLS_COLORTRANSFORM
#define CHARLS_COLORTRANSFORM

#include "util.h"

// This file defines simple classes that define (lossless) color transforms.
// They are invoked in processline.h to convert between decoded values and the internal line buffers.
// Color transforms work best for computer generated images, but are outside the official JPEG-LS specifications.

template<typename T>
struct TransformNoneImpl
{
    static_assert(std::is_integral<T>::value, "Integral required.");

    using size_type = T;

    FORCE_INLINE Triplet<T> operator()(int v1, int v2, int v3) const noexcept
    {
        return Triplet<T>(v1, v2, v3);
    }
};


template<typename T>
struct TransformNone : TransformNoneImpl<T>
{
    static_assert(std::is_integral<T>::value, "Integral required.");

    using Inverse = TransformNoneImpl<T>;
};


template<typename T>
struct TransformHp1
{
    static_assert(std::is_integral<T>::value, "Integral required.");

    using size_type = T;

    struct Inverse
    {
        explicit Inverse(const TransformHp1&) noexcept
        {
        }

        FORCE_INLINE Triplet<T> operator()(int v1, int v2, int v3) const noexcept
        {
            return Triplet<T>(v1 + v2 - Range / 2, v2, v3 + v2 - Range / 2);
        }
    };

    FORCE_INLINE Triplet<T> operator()(int red, int green, int blue) const noexcept
    {
        Triplet<T> hp1;
        hp1.v2 = static_cast<T>(green);
        hp1.v1 = static_cast<T>(red - green + Range / 2);
        hp1.v3 = static_cast<T>(blue - green + Range / 2);
        return hp1;
    }

private:
    static constexpr size_t Range = 1 << (sizeof(T) * 8);
};


template<typename T>
struct TransformHp2
{
    static_assert(std::is_integral<T>::value, "Integral required.");

    using size_type = T;

    struct Inverse
    {
        explicit Inverse(const TransformHp2&) noexcept
        {
        }

        FORCE_INLINE Triplet<T> operator()(int v1, int v2, int v3) const noexcept
        {
            Triplet<T> rgb;
            rgb.R = static_cast<T>(v1 + v2 - Range / 2);                     // new R
            rgb.G = static_cast<T>(v2);                                      // new G
            rgb.B = static_cast<T>(v3 + ((rgb.R + rgb.G) >> 1) - Range / 2); // new B
            return rgb;
        }
    };

    FORCE_INLINE Triplet<T> operator()(int red, int green, int blue) const noexcept
    {
        return Triplet<T>(red - green + Range / 2, green, blue - ((red + green) >> 1) - Range / 2);
    }

private:
    static constexpr size_t Range = 1 << (sizeof(T) * 8);
};


template<typename T>
struct TransformHp3
{
    static_assert(std::is_integral<T>::value, "Integral required.");

    using size_type = T;

    struct Inverse
    {
        explicit Inverse(const TransformHp3&) noexcept
        {
        }

        FORCE_INLINE Triplet<T> operator()(int v1, int v2, int v3) const noexcept
        {
            const int G = v1 - ((v3 + v2) >> 2) + Range / 4;
            Triplet<T> rgb;
            rgb.R = static_cast<T>(v3 + G - Range / 2); // new R
            rgb.G = static_cast<T>(G);                  // new G
            rgb.B = static_cast<T>(v2 + G - Range / 2); // new B
            return rgb;
        }
    };

    FORCE_INLINE Triplet<T> operator()(int red, int green, int blue) const noexcept
    {
        Triplet<T> hp3;
        hp3.v2 = static_cast<T>(blue - green + Range / 2);
        hp3.v3 = static_cast<T>(red - green + Range / 2);
        hp3.v1 = static_cast<T>(green + ((hp3.v2 + hp3.v3) >> 2)) - Range / 4;
        return hp3;
    }

private:
    static constexpr size_t Range = 1 << (sizeof(T) * 8);
};


// Transform class that shifts bits towards the high bit when bit count is not 8 or 16
// needed to make the HP color transformations work correctly.
template<typename Transform>
struct TransformShifted
{
    using size_type = typename Transform::size_type;

    struct Inverse
    {
        explicit Inverse(const TransformShifted& transform) noexcept
            : _shift(transform._shift),
              _inverseTransform(transform._colortransform)
        {
        }

        FORCE_INLINE Triplet<size_type> operator()(int v1, int v2, int v3) noexcept
        {
            const Triplet<size_type> result = _inverseTransform(v1 << _shift, v2 << _shift, v3 << _shift);
            return Triplet<size_type>(result.R >> _shift, result.G >> _shift, result.B >> _shift);
        }

        FORCE_INLINE Quad<size_type> operator()(int v1, int v2, int v3, int v4)
        {
            Triplet<size_type> result = _inverseTransform(v1 << _shift, v2 << _shift, v3 << _shift);
            return Quad<size_type>(result.R >> _shift, result.G >> _shift, result.B >> _shift, v4);
        }

    private:
        int _shift;
        typename Transform::Inverse _inverseTransform;
    };

    explicit TransformShifted(int shift) noexcept
        : _shift(shift)
    {
    }

    FORCE_INLINE Triplet<size_type> operator()(int red, int green, int blue) noexcept
    {
        const Triplet<size_type> result = _colortransform(red << _shift, green << _shift, blue << _shift);
        return Triplet<size_type>(result.R >> _shift, result.G >> _shift, result.B >> _shift);
    }

    FORCE_INLINE Quad<size_type> operator()(int red, int green, int blue, int alpha)
    {
        Triplet<size_type> result = _colortransform(red << _shift, green << _shift, blue << _shift);
        return Quad<size_type>(result.R >> _shift, result.G >> _shift, result.B >> _shift, alpha);
    }

private:
    int _shift;
    Transform _colortransform;
};


#endif
