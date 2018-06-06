//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//
#ifndef CHARLS_JPEGSTREAMREADER
#define CHARLS_JPEGSTREAMREADER

#include "publictypes.h"
#include <cstdint>
#include <vector>


enum class JpegMarkerCode : uint8_t;
struct JlsParameters;
class JpegCustomParameters;


JpegLSPresetCodingParameters ComputeDefault(int32_t maximumSampleValue, int32_t allowedLossyError) noexcept;


//
// JpegStreamReader: minimal implementation to read a JPEG byte stream.
//
class JpegStreamReader
{
public:
    explicit JpegStreamReader(ByteStreamInfo byteStreamInfo) noexcept;

    const JlsParameters& GetMetadata() const noexcept
    {
        return _params;
    }

    const JpegLSPresetCodingParameters& GetCustomPreset() const noexcept
    {
        return _params.custom;
    }

    void Read(ByteStreamInfo rawPixels);
    void ReadHeader();

    void SetInfo(const JlsParameters& params) noexcept
    {
        _params = params;
    }

    void SetRect(const JlsRect& rect) noexcept
    {
        _rect = rect;
    }

    void ReadStartOfScan(bool firstComponent);
    uint8_t ReadByte();

private:
    JpegMarkerCode ReadNextMarker();
    int ReadPresetParameters();
    static int ReadComment() noexcept;
    int ReadStartOfFrame();
    int ReadWord();
    void ReadNBytes(std::vector<char>& dst, int byteCount);
    int ReadMarker(JpegMarkerCode marker);

    void ReadJfif();

    // Color Transform Application Markers & Code Stream (HP extension)
    int ReadColorSpace() const noexcept;
    int ReadColorXForm();

    ByteStreamInfo _byteStream;
    JlsParameters _params;
    JlsRect _rect;
};


#endif
