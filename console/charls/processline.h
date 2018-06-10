//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//
#ifndef CHARLS_PROCESSLINE
#define CHARLS_PROCESSLINE

#include "util.h"
#include "publictypes.h"
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>


//
// This file defines the ProcessLine base class, its derivatives and helper functions.
// During coding/decoding, CharLS process one line at a time. The different Processline implementations
// convert the uncompressed format to and from the internal format for encoding.
// Conversions include color transforms, line interleaved vs sample interleaved, masking out unused bits,
// accounting for line padding etc.
// This mechanism could be used to encode/decode images as they are received.
//

class ProcessLine
{
public:
    virtual ~ProcessLine() = default;

    ProcessLine(const ProcessLine&) = delete;
    ProcessLine(ProcessLine&&) = delete;
    ProcessLine& operator=(const ProcessLine&) = delete;
    ProcessLine& operator=(ProcessLine&&) = delete;

    virtual void NewLineDecoded(const void* pSrc, int pixelCount, int sourceStride) = 0;
    virtual void NewLineRequested(void* pDest, int pixelCount, int destStride) = 0;

protected:
    ProcessLine() = default;
};


class PostProcesSingleComponent : public ProcessLine
{
public:
    PostProcesSingleComponent(void* rawData, const JlsParameters& params, size_t bytesPerPixel) noexcept :
        _rawData(static_cast<uint8_t*>(rawData)),
        _bytesPerPixel(bytesPerPixel),
        _bytesPerLine(params.stride)
    {
    }

    WARNING_SUPPRESS(26440)
    void NewLineRequested(void* dest, int pixelCount, int /*byteStride*/) override
    {
        std::memcpy(dest, _rawData, pixelCount * _bytesPerPixel);
        _rawData += _bytesPerLine;
    }

    void NewLineDecoded(const void* pSrc, int pixelCount, int /*sourceStride*/) override
    {
        std::memcpy(_rawData, pSrc, pixelCount * _bytesPerPixel);
        _rawData += _bytesPerLine;
    }
    WARNING_UNSUPPRESS()

private:
    uint8_t* _rawData;
    size_t _bytesPerPixel;
    size_t _bytesPerLine;
};


inline void ByteSwap(unsigned char* data, int count)
{
    if (static_cast<unsigned int>(count) & 1u)
    {
        std::ostringstream message;
        message << "An odd number of bytes (" << count << ") cannot be swapped.";
        throw charls_error(charls::ApiResult::InvalidJlsParameters, message.str());
    }

    const auto data32 = reinterpret_cast<unsigned int*>(data);
    for(auto i = 0; i < count / 4; i++)
    {
        const auto value = data32[i];
        data32[i] = ((value >> 8u) & 0x00FF00FFu) | ((value & 0x00FF00FFu) << 8u);
    }

    if ((count % 4) != 0)
    {
        std::swap(data[count-2], data[count-1]);
    }
}

class PostProcesSingleStream : public ProcessLine
{
public:
    PostProcesSingleStream(std::basic_streambuf<char>* rawData, const JlsParameters& params, size_t bytesPerPixel) noexcept :
        _rawData(rawData),
        _bytesPerPixel(bytesPerPixel),
        _bytesPerLine(params.stride)
    {
    }

    void NewLineRequested(void* dest, int pixelCount, int /*destStride*/) override
    {
        auto bytesToRead = pixelCount * _bytesPerPixel;
        while (bytesToRead != 0)
        {
            const auto bytesRead = _rawData->sgetn(static_cast<char*>(dest), bytesToRead);
            if (bytesRead == 0)
                throw charls_error(charls::ApiResult::UncompressedBufferTooSmall);

            bytesToRead = static_cast<std::size_t>(bytesToRead - bytesRead);
        }

        if (_bytesPerPixel == 2)
        {
            ByteSwap(static_cast<unsigned char*>(dest), 2 * pixelCount);
        }

        if (_bytesPerLine - pixelCount * _bytesPerPixel > 0)
        {
            _rawData->pubseekoff(static_cast<std::streamoff>(_bytesPerLine - bytesToRead), std::ios_base::cur);
        }
    }

    void NewLineDecoded(const void* pSrc, int pixelCount, int /*sourceStride*/) override
    {
        const auto bytesToWrite = pixelCount * _bytesPerPixel;
        const auto bytesWritten = static_cast<size_t>(_rawData->sputn(static_cast<const char*>(pSrc), bytesToWrite));
        if (bytesWritten != bytesToWrite)
            throw charls_error(charls::ApiResult::UncompressedBufferTooSmall);
    }

private:
    std::basic_streambuf<char>* _rawData;
    size_t _bytesPerPixel;
    size_t _bytesPerLine;
};


template<typename TRANSFORM, typename T>
void TransformLineToQuad(const T* ptypeInput, int32_t pixelStrideIn, Quad<T>* pbyteBuffer, int32_t pixelStride, TRANSFORM& transform) noexcept
{
    const int cpixel = std::min(pixelStride, pixelStrideIn);
    Quad<T>* ptypeBuffer = pbyteBuffer;

    for (auto x = 0; x < cpixel; ++x)
    {
        const Quad<T> pixel(transform(ptypeInput[x], ptypeInput[x + pixelStrideIn], ptypeInput[x + 2*pixelStrideIn]), ptypeInput[x + 3 * pixelStrideIn]);
        ptypeBuffer[x] = pixel;
    }
}


template<typename TRANSFORM, typename T>
void TransformQuadToLine(const Quad<T>* pbyteInput, int32_t pixelStrideIn, T* ptypeBuffer, int32_t pixelStride, TRANSFORM& transform) noexcept
{
    const auto cpixel = std::min(pixelStride, pixelStrideIn);
    const Quad<T>* ptypeBufferIn = pbyteInput;

    for (auto x = 0; x < cpixel; ++x)
    {
        const Quad<T> color = ptypeBufferIn[x];
        const Quad<T> colorTranformed(transform(color.v1, color.v2, color.v3), color.v4);

        ptypeBuffer[x] = colorTranformed.v1;
        ptypeBuffer[x + pixelStride] = colorTranformed.v2;
        ptypeBuffer[x + 2 * pixelStride] = colorTranformed.v3;
        ptypeBuffer[x + 3 * pixelStride] = colorTranformed.v4;
    }
}


template<typename T>
void TransformRgbToBgr(T* pDest, int samplesPerPixel, int pixelCount) noexcept
{
    for (auto i = 0; i < pixelCount; ++i)
    {
        std::swap(pDest[0], pDest[2]);
        pDest += samplesPerPixel;
    }
}


template<typename TRANSFORM, typename T>
void TransformLine(Triplet<T>* pDest, const Triplet<T>* pSrc, int pixelCount, TRANSFORM& transform) noexcept
{
    for (auto i = 0; i < pixelCount; ++i)
    {
        pDest[i] = transform(pSrc[i].v1, pSrc[i].v2, pSrc[i].v3);
    }
}


template<typename TRANSFORM, typename T>
void TransformLineToTriplet(const T* ptypeInput, int32_t pixelStrideIn, Triplet<T>* pbyteBuffer, int32_t pixelStride, TRANSFORM& transform) noexcept
{
    const auto cpixel = std::min(pixelStride, pixelStrideIn);
    Triplet<T>* ptypeBuffer = pbyteBuffer;

    for (auto x = 0; x < cpixel; ++x)
    {
        ptypeBuffer[x] = transform(ptypeInput[x], ptypeInput[x + pixelStrideIn], ptypeInput[x + 2*pixelStrideIn]);
    }
}


template<typename TRANSFORM, typename T>
void TransformTripletToLine(const Triplet<T>* pbyteInput, int32_t pixelStrideIn, T* ptypeBuffer, int32_t pixelStride, TRANSFORM& transform) noexcept
{
    const auto cpixel = std::min(pixelStride, pixelStrideIn);
    const Triplet<T>* ptypeBufferIn = pbyteInput;

    for (auto x = 0; x < cpixel; ++x)
    {
        const Triplet<T> color = ptypeBufferIn[x];
        const Triplet<T> colorTranformed = transform(color.v1, color.v2, color.v3);

        ptypeBuffer[x] = colorTranformed.v1;
        ptypeBuffer[x + pixelStride] = colorTranformed.v2;
        ptypeBuffer[x + 2 *pixelStride] = colorTranformed.v3;
    }
}


template<typename TRANSFORM>
class ProcessTransformed : public ProcessLine
{
public:
    ProcessTransformed(ByteStreamInfo rawStream, const JlsParameters& info, TRANSFORM transform) :
        _params(info),
        _templine(static_cast<size_t>(info.width) * info.components),
        _buffer(static_cast<size_t>(info.width) * info.components * sizeof(size_type)),
        _transform(transform),
        _inverseTransform(transform),
        _rawPixels(rawStream)
    {
    }

    void NewLineRequested(void* dest, int pixelCount, int destStride) override
    {
        if (!_rawPixels.rawStream)
        {
            Transform(_rawPixels.rawData, dest, pixelCount, destStride);
            _rawPixels.rawData += _params.stride;
            return;
        }

        Transform(_rawPixels.rawStream, dest, pixelCount, destStride);
    }

    void Transform(std::basic_streambuf<char>* rawStream, void* dest, int pixelCount, int destStride)
    {
        std::streamsize bytesToRead = static_cast<std::streamsize>(pixelCount) * _params.components * sizeof(size_type);
        while (bytesToRead != 0)
        {
            const auto read = rawStream->sgetn(reinterpret_cast<char*>(_buffer.data()), bytesToRead);
            if (read == 0)
            {
                std::ostringstream message;
                message << "No more bytes available in input buffer, still needing " << read;
                throw charls_error(charls::ApiResult::UncompressedBufferTooSmall, message.str());
            }

            bytesToRead -= read;
        }
        Transform(_buffer.data(), dest, pixelCount, destStride);
    }

    void Transform(const void* source, void* dest, int pixelCount, int destStride) noexcept
    {
        if (_params.outputBgr)
        {
            memcpy(_templine.data(), source, sizeof(Triplet<size_type>) * pixelCount);
            TransformRgbToBgr(_templine.data(), _params.components, pixelCount);
            source = _templine.data();
        }

        if (_params.components == 3)
        {
            if (_params.interleaveMode == charls::InterleaveMode::Sample)
            {
                TransformLine(static_cast<Triplet<size_type>*>(dest), static_cast<const Triplet<size_type>*>(source), pixelCount, _transform);
            }
            else
            {
                TransformTripletToLine(static_cast<const Triplet<size_type>*>(source), pixelCount, static_cast<size_type*>(dest), destStride, _transform);
            }
        }
        else if (_params.components == 4 && _params.interleaveMode == charls::InterleaveMode::Line)
        {
            TransformQuadToLine(static_cast<const Quad<size_type>*>(source), pixelCount, static_cast<size_type*>(dest), destStride, _transform);
        }
    }

    void DecodeTransform(const void* pSrc, void* rawData, int pixelCount, int byteStride) noexcept
    {
        if (_params.components == 3)
        {
            if (_params.interleaveMode == charls::InterleaveMode::Sample)
            {
                TransformLine(static_cast<Triplet<size_type>*>(rawData), static_cast<const Triplet<size_type>*>(pSrc), pixelCount, _inverseTransform);
            }
            else
            {
                TransformLineToTriplet(static_cast<const size_type*>(pSrc), byteStride, static_cast<Triplet<size_type>*>(rawData), pixelCount, _inverseTransform);
            }
        }
        else if (_params.components == 4 && _params.interleaveMode == charls::InterleaveMode::Line)
        {
            TransformLineToQuad(static_cast<const size_type*>(pSrc), byteStride, static_cast<Quad<size_type>*>(rawData), pixelCount, _inverseTransform);
        }

        if (_params.outputBgr)
        {
            TransformRgbToBgr(static_cast<size_type*>(rawData), _params.components, pixelCount);
        }
    }

    void NewLineDecoded(const void* pSrc, int pixelCount, int sourceStride) override
    {
        if (_rawPixels.rawStream)
        {
            const std::streamsize bytesToWrite = static_cast<std::streamsize>(pixelCount) * _params.components * sizeof(size_type);
            DecodeTransform(pSrc, _buffer.data(), pixelCount, sourceStride);

            const auto bytesWritten = _rawPixels.rawStream->sputn(reinterpret_cast<char*>(_buffer.data()), bytesToWrite);
            if (bytesWritten != bytesToWrite)
                throw charls_error(charls::ApiResult::UncompressedBufferTooSmall);
        }
        else
        {
            DecodeTransform(pSrc, _rawPixels.rawData, pixelCount, sourceStride);
            _rawPixels.rawData += _params.stride;
        }
    }

private:
    using size_type = typename TRANSFORM::size_type;

    const JlsParameters& _params;
    std::vector<size_type> _templine;
    std::vector<uint8_t> _buffer;
    TRANSFORM _transform;
    typename TRANSFORM::Inverse _inverseTransform;
    ByteStreamInfo _rawPixels;
};


#endif
