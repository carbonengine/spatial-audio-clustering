/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the
"Apache License"); you may not use this file except in compliance with the
Apache License. You may obtain a copy of the Apache License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2023 Audiokinetic Inc.
*******************************************************************************/

#include "SpatialClusteringFXParams.h"

#include <AK/Tools/Common/AkBankReadHelpers.h>
#include <AK/Tools/Common/AkPlatformFuncs.h>

#include <cmath>

namespace
{
    /** @brief Default values of the parameters that RTPCs can drive. */
    const SpatialClusteringRTPCParams kDefaultRTPCParams = { 15.f, 0, 100, 0.f, 200.f, 50.f, 0.f, 1.f };

    /** @brief Returns true if every real parameter is a finite number. */
    bool AreFinite(const SpatialClusteringRTPCParams& in_params)
    {
        return std::isfinite(in_params.joinAngle)
            && std::isfinite(in_params.nearFieldRadius)
            && std::isfinite(in_params.dwellTime)
            && std::isfinite(in_params.crossfadeTime)
            && std::isfinite(in_params.distanceWeight)
            && std::isfinite(in_params.spreadFromSize);
    }

    /** @brief Reads a value sent as a float and returns false if it is missing or not finite. */
    bool ReadReal(const void* in_pValue, AkUInt32 in_ulParamSize, AkReal32& out_value)
    {
        if (in_pValue == nullptr || in_ulParamSize < sizeof(AkReal32))
            return false;

        AkReal32 value;
        AKPLATFORM::AkMemCpy(&value, in_pValue, sizeof(AkReal32));
        if (!std::isfinite(value))
            return false;

        out_value = value;
        return true;
    }

    /** @brief Rounds a float to the nearest integer between 0 and a limit. */
    AkInt32 RoundToRange(AkReal32 in_value, AkReal32 in_limit)
    {
        const AkReal32 clamped = in_value < 0.f ? 0.f : (in_value > in_limit ? in_limit : in_value);
        return static_cast<AkInt32>(clamped + 0.5f);
    }
}

SpatialClusteringFXParams::SpatialClusteringFXParams()
    : RTPC(kDefaultRTPCParams)
{
}

SpatialClusteringFXParams::~SpatialClusteringFXParams()
{
}

SpatialClusteringFXParams::SpatialClusteringFXParams(const SpatialClusteringFXParams& in_rParams)
{
    RTPC = in_rParams.RTPC;
    m_paramChangeHandler.SetAllParamChanges();
}

AK::IAkPluginParam* SpatialClusteringFXParams::Clone(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, SpatialClusteringFXParams(*this));
}

AKRESULT SpatialClusteringFXParams::Init(AK::IAkPluginMemAlloc* in_pAllocator, const void* in_pParamsBlock, AkUInt32 in_ulBlockSize)
{
    (void)in_pAllocator;

    if (in_ulBlockSize == 0)
    {
        // Initialize default parameters here
        RTPC = kDefaultRTPCParams;
        m_paramChangeHandler.SetAllParamChanges();
        return AK_Success;
    }

    return SetParamsBlock(in_pParamsBlock, in_ulBlockSize);
}

AKRESULT SpatialClusteringFXParams::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT SpatialClusteringFXParams::SetParamsBlock(const void* in_pParamsBlock, AkUInt32 in_ulBlockSize)
{
    if (in_pParamsBlock == nullptr || in_ulBlockSize != PARAMS_BLOCK_SIZE)
        return AK_InvalidParameter;

    AKRESULT eResult = AK_Success;
    AkUInt8* pParamsBlock = (AkUInt8*)in_pParamsBlock;

    SpatialClusteringRTPCParams rtpc;
    rtpc.joinAngle = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    rtpc.maxOutputs = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
    rtpc.excludePriorityAbove = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
    rtpc.nearFieldRadius = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    rtpc.dwellTime = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    rtpc.crossfadeTime = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    rtpc.distanceWeight = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    rtpc.spreadFromSize = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    CHECKBANKDATASIZE(in_ulBlockSize, eResult);

    if (!AreFinite(rtpc))
        return AK_InvalidParameter;

    RTPC = rtpc;
    m_paramChangeHandler.SetAllParamChanges();

    return eResult;
}

AKRESULT SpatialClusteringFXParams::SetParam(AkPluginParamID in_paramID, const void* in_pValue, AkUInt32 in_ulParamSize)
{
    AkReal32 value = 0.f;

    // Handle parameter change here
    switch (in_paramID)
    {
    case PARAM_JOIN_ANGLE_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.joinAngle = value;
        break;
    case PARAM_DWELL_TIME_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.dwellTime = value;
        break;
    case PARAM_CROSSFADE_TIME_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.crossfadeTime = value;
        break;
    case PARAM_MAX_OUTPUTS_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.maxOutputs = RoundToRange(value, static_cast<AkReal32>(PARAM_MAX_OUTPUTS_MAX));
        break;
    case PARAM_NEAR_FIELD_RADIUS_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.nearFieldRadius = value;
        break;
    case PARAM_DISTANCE_WEIGHT_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.distanceWeight = value;
        break;
    case PARAM_EXCLUDE_PRIORITY_ABOVE_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.excludePriorityAbove = RoundToRange(value, static_cast<AkReal32>(PARAM_PRIORITY_MAX));
        break;
    case PARAM_SPREAD_FROM_SIZE_ID:
        if (!ReadReal(in_pValue, in_ulParamSize, value))
            return AK_InvalidParameter;
        RTPC.spreadFromSize = value;
        break;
    default:
        return AK_InvalidParameter;
    }

    m_paramChangeHandler.SetParamChange(in_paramID);
    return AK_Success;
}
