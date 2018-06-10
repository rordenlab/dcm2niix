//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//


#ifndef CHARLS_DEFAULTTRAITS
#define CHARLS_DEFAULTTRAITS


#include "util.h"
#include "constants.h"
#include <algorithm>
#include <cstdlib>


// Default traits that support all JPEG LS parameters: custom limit, near, maxval (not power of 2)

// This traits class is used to initialize a coder/decoder.
// The coder/decoder also delegates some functions to the traits class.
// This is to allow the traits class to replace the default implementation here with optimized specific implementations.
// This is done for lossless coding/decoding: see losslesstraits.h

WARNING_SUPPRESS(26432)

template<typename sample, typename pixel>
struct DefaultTraits
{
    using SAMPLE = sample;
    using PIXEL = pixel;

    int32_t MAXVAL;
    const int32_t RANGE;
    const int32_t NEAR;
    const int32_t qbpp;
    const int32_t bpp;
    const int32_t LIMIT;
    const int32_t RESET;

    DefaultTraits(int32_t max, int32_t near, int32_t reset = DefaultResetValue) noexcept :
        MAXVAL(max),
        RANGE((max + 2 * near) / (2 * near + 1) + 1),
        NEAR(near),
        qbpp(log_2(RANGE)),
        bpp(log_2(max)),
        LIMIT(2 * (bpp + std::max(8, bpp))),
        RESET(reset)
    {
    }

    DefaultTraits(const DefaultTraits& other) noexcept :
        MAXVAL(other.MAXVAL),
        RANGE(other.RANGE),
        NEAR(other.NEAR),
        qbpp(other.qbpp),
        bpp(other.bpp),
        LIMIT(other.LIMIT),
        RESET(other.RESET)
    {
    }

    DefaultTraits() = delete;
    DefaultTraits(DefaultTraits&&) = default;
    DefaultTraits& operator=(const DefaultTraits&) = delete;
    DefaultTraits& operator=(DefaultTraits&&) = delete;

    FORCE_INLINE int32_t ComputeErrVal(int32_t e) const noexcept
    {
        return ModuloRange(Quantize(e));
    }

    FORCE_INLINE SAMPLE ComputeReconstructedSample(int32_t Px, int32_t ErrVal) const noexcept
    {
        return FixReconstructedValue(Px + DeQuantize(ErrVal));
    }

    FORCE_INLINE bool IsNear(int32_t lhs, int32_t rhs) const noexcept
    {
        return std::abs(lhs - rhs) <= NEAR;
    }

    bool IsNear(Triplet<SAMPLE> lhs, Triplet<SAMPLE> rhs) const noexcept
    {
        return std::abs(lhs.v1 - rhs.v1) <= NEAR &&
               std::abs(lhs.v2 - rhs.v2) <= NEAR &&
               std::abs(lhs.v3 - rhs.v3) <= NEAR;
    }

    FORCE_INLINE int32_t CorrectPrediction(int32_t Pxc) const noexcept
    {
        if ((Pxc & MAXVAL) == Pxc)
            return Pxc;

        return (~(Pxc >> (int32_t_bit_count-1))) & MAXVAL;
    }

    /// <summary>
    /// Returns the value of errorValue modulo RANGE. ITU.T.87, A.4.5 (code segment A.9)
    /// </summary>
    FORCE_INLINE int32_t ModuloRange(int32_t errorValue) const noexcept
    {
        ASSERT(std::abs(errorValue) <= RANGE);

        if (errorValue < 0)
        {
            errorValue += RANGE;
        }
        if (errorValue >= (RANGE + 1) / 2)
        {
            errorValue -= RANGE;
        }

        ASSERT(-RANGE / 2 <= errorValue && errorValue <= (RANGE / 2) - 1);
        return errorValue;
    }

private:
    int32_t Quantize(int32_t Errval) const noexcept
    {
        if (Errval > 0)
            return  (Errval + NEAR) / (2 * NEAR + 1);

        return - (NEAR - Errval) / (2 * NEAR + 1);
    }

    FORCE_INLINE int32_t DeQuantize(int32_t Errval) const noexcept
    {
        return Errval * (2 * NEAR + 1);
    }

    FORCE_INLINE SAMPLE FixReconstructedValue(int32_t val) const noexcept
    {
        if (val < -NEAR)
        {
            val = val + RANGE * (2 * NEAR + 1);
        }
        else if (val > MAXVAL + NEAR)
        {
            val = val - RANGE * (2 * NEAR + 1);
        }

        return static_cast<SAMPLE>(CorrectPrediction(val));
    }
};

WARNING_UNSUPPRESS()

#endif
