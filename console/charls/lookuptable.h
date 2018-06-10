//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//


#ifndef CHARLS_LOOKUPTABLE
#define CHARLS_LOOKUPTABLE


#include <cstring>


// Tables for fast decoding of short Golomb Codes.
struct Code
{
    Code() noexcept :
        _value(),
        _length()
    {
    }

    Code(int32_t value, int32_t length) noexcept :
        _value(value),
        _length(length)
    {
    }

    int32_t GetValue() const noexcept
    {
        return _value;
    }

    int32_t GetLength() const noexcept
    {
        return _length;
    }

    int32_t _value;
    int32_t _length;
};


class CTable
{
public:
    static constexpr size_t byte_bit_count = 8;

    CTable() noexcept
    {
        std::memset(_rgtype, 0, sizeof(_rgtype));
    }

    void AddEntry(uint8_t bvalue, Code c) noexcept
    {
        const int32_t length = c.GetLength();
        ASSERT(static_cast<size_t>(length) <= byte_bit_count);

        for (int32_t i = 0; i < static_cast<int32_t>(1) << (byte_bit_count - length); ++i)
        {
            ASSERT(_rgtype[(bvalue << (byte_bit_count - length)) + i].GetLength() == 0);
            _rgtype[(bvalue << (byte_bit_count - length)) + i] = c;
        }
    }

    FORCE_INLINE const Code& Get(int32_t value) const noexcept
    {
        return _rgtype[value];
    }

private:
    Code _rgtype[1 << byte_bit_count];
};


#endif
