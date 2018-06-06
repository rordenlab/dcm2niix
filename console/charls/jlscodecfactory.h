//
// (C) CharLS Team 2014, all rights reserved. See the accompanying "License.txt" for licensed use.
//

#ifndef CHARLS_JLSCODECFACTORY
#define CHARLS_JLSCODECFACTORY

#include <memory>

struct JlsParameters;
struct JpegLSPresetCodingParameters;

template<typename Strategy>
class JlsCodecFactory
{
public:
    std::unique_ptr<Strategy> CreateCodec(const JlsParameters& params, const JpegLSPresetCodingParameters& presets);

private:
    std::unique_ptr<Strategy> CreateOptimizedCodec(const JlsParameters& params);
};

#endif
