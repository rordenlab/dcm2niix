//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//

#include "util.h"
#include "decoderstrategy.h"
#include "encoderstrategy.h"
#include "lookuptable.h"
#include "losslesstraits.h"
#include "defaulttraits.h"
#include "jlscodecfactory.h"
#include "jpegstreamreader.h"
#include <vector>

using namespace charls;

// As defined in the JPEG-LS standard

// used to determine how large runs should be encoded at a time.
const int J[32] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15};

#include "scan.h"

namespace
{

signed char QuantizeGratientOrg(const JpegLSPresetCodingParameters& preset, int32_t NEAR, int32_t Di) noexcept
{
    if (Di <= -preset.Threshold3) return  -4;
    if (Di <= -preset.Threshold2) return  -3;
    if (Di <= -preset.Threshold1) return  -2;
    if (Di < -NEAR)  return  -1;
    if (Di <=  NEAR) return   0;
    if (Di < preset.Threshold1)   return   1;
    if (Di < preset.Threshold2)   return   2;
    if (Di < preset.Threshold3)   return   3;

    return  4;
}


std::vector<signed char> CreateQLutLossless(int32_t cbit)
{
    const JpegLSPresetCodingParameters preset = ComputeDefault((1u << static_cast<uint32_t>(cbit)) - 1, 0);
    const int32_t range = preset.MaximumSampleValue + 1;

    std::vector<signed char> lut(static_cast<size_t>(range) * 2);

    for (int32_t diff = -range; diff < range; diff++)
    {
        lut[static_cast<size_t>(range) + diff] = QuantizeGratientOrg(preset, 0,diff);
    }
    return lut;
}

template<typename Strategy, typename Traits>
std::unique_ptr<Strategy> create_codec(const Traits& traits, const JlsParameters& params)
{
    return std::make_unique<JlsCodec<Traits, Strategy>>(traits, params);
}


} // namespace


class charls_category : public std::error_category
{
public:
    const char* name() const noexcept override
    {
        return "charls";
    }

    std::string message(int /* errval */) const override
    {
        return "CharLS error";
    }
};

const std::error_category& charls_error::CharLSCategoryInstance() noexcept
{
    static charls_category instance;
    return instance;
}


// Lookup tables to replace code with lookup tables.
// To avoid threading issues, all tables are created when the program is loaded.

// Lookup table: decode symbols that are smaller or equal to 8 bit (16 tables for each value of k)
CTable decodingTables[16] = { InitTable(0), InitTable(1), InitTable(2), InitTable(3),
                              InitTable(4), InitTable(5), InitTable(6), InitTable(7),
                              InitTable(8), InitTable(9), InitTable(10), InitTable(11),
                              InitTable(12), InitTable(13), InitTable(14),InitTable(15) };

// Lookup tables: sample differences to bin indexes.
std::vector<signed char> rgquant8Ll = CreateQLutLossless(8);
std::vector<signed char> rgquant10Ll = CreateQLutLossless(10);
std::vector<signed char> rgquant12Ll = CreateQLutLossless(12);
std::vector<signed char> rgquant16Ll = CreateQLutLossless(16);


template<typename Strategy>
std::unique_ptr<Strategy> JlsCodecFactory<Strategy>::CreateCodec(const JlsParameters& params, const JpegLSPresetCodingParameters& presets)
{
    std::unique_ptr<Strategy> codec;

    if (presets.ResetValue == 0 || presets.ResetValue == DefaultResetValue)
    {
        codec = CreateOptimizedCodec(params);
    }

    if (!codec)
    {
        if (params.bitsPerSample <= 8)
        {
            DefaultTraits<uint8_t, uint8_t> traits((1 << params.bitsPerSample) - 1, params.allowedLossyError, presets.ResetValue);
            traits.MAXVAL = presets.MaximumSampleValue;
            codec = std::make_unique<JlsCodec<DefaultTraits<uint8_t, uint8_t>, Strategy>>(traits, params);
        }
        else
        {
            DefaultTraits<uint16_t, uint16_t> traits((1 << params.bitsPerSample) - 1, params.allowedLossyError, presets.ResetValue);
            traits.MAXVAL = presets.MaximumSampleValue;
            codec = std::make_unique<JlsCodec<DefaultTraits<uint16_t, uint16_t>, Strategy>>(traits, params);
        }
    }

    codec->SetPresets(presets);
    return codec;
}

template<typename Strategy>
std::unique_ptr<Strategy> JlsCodecFactory<Strategy>::CreateOptimizedCodec(const JlsParameters& params)
{
    if (params.interleaveMode == InterleaveMode::Sample && params.components != 3)
        return nullptr;

#ifndef DISABLE_SPECIALIZATIONS

    // optimized lossless versions common formats
    if (params.allowedLossyError == 0)
    {
        if (params.interleaveMode == InterleaveMode::Sample)
        {
            if (params.bitsPerSample == 8)
                return create_codec<Strategy>(LosslessTraits<Triplet<uint8_t>, 8>(), params);
        }
        else
        {
            switch (params.bitsPerSample)
            {
            case  8: return create_codec<Strategy>(LosslessTraits<uint8_t, 8>(), params);
            case 12: return create_codec<Strategy>(LosslessTraits<uint16_t, 12>(), params);
            case 16: return create_codec<Strategy>(LosslessTraits<uint16_t, 16>(), params);
                default:
                    break;
            }
        }
    }

#endif

    const int maxval = (1u << static_cast<unsigned int>(params.bitsPerSample)) - 1;

    if (params.bitsPerSample <= 8)
    {
        if (params.interleaveMode == InterleaveMode::Sample)
            return create_codec<Strategy>(DefaultTraits<uint8_t, Triplet<uint8_t> >(maxval, params.allowedLossyError), params);

        return create_codec<Strategy>(DefaultTraits<uint8_t, uint8_t>((1u << params.bitsPerSample) - 1, params.allowedLossyError), params);
    }
    if (params.bitsPerSample <= 16)
    {
        if (params.interleaveMode == InterleaveMode::Sample)
            return create_codec<Strategy>(DefaultTraits<uint16_t,Triplet<uint16_t> >(maxval, params.allowedLossyError), params);

        return create_codec<Strategy>(DefaultTraits<uint16_t, uint16_t>(maxval, params.allowedLossyError), params);
    }
    return nullptr;
}


template class JlsCodecFactory<DecoderStrategy>;
template class JlsCodecFactory<EncoderStrategy>;
