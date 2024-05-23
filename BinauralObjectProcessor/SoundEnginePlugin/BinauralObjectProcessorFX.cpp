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

#include "BinauralObjectProcessorFX.h"
#include "../BinauralObjectProcessorConfig.h"

#include <AK/AkWwiseSDKVersion.h>

AK::IAkPlugin* CreateBinauralObjectProcessorFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
	return AK_PLUGIN_NEW(in_pAllocator, BinauralObjectProcessorFX());
}

AK::IAkPluginParam* CreateBinauralObjectProcessorFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
	return AK_PLUGIN_NEW(in_pAllocator, BinauralObjectProcessorFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(BinauralObjectProcessorFX, AkPluginTypeEffect, BinauralObjectProcessorConfig::CompanyID, BinauralObjectProcessorConfig::PluginID)

BinauralObjectProcessorFX::BinauralObjectProcessorFX()
	: m_pParams(nullptr)
	, m_pAllocator(nullptr)
	, m_pContext(nullptr)
{
}

BinauralObjectProcessorFX::~BinauralObjectProcessorFX()
{
}

AKRESULT BinauralObjectProcessorFX::Init(
	AK::IAkPluginMemAlloc* in_pAllocator,
	AK::IAkEffectPluginContext* in_pContext,
	AK::IAkPluginParam* in_pParams,
	AkAudioFormat& in_rFormat)
{
	m_pParams = (BinauralObjectProcessorFXParams*)in_pParams;
	m_pAllocator = in_pAllocator;
	m_pContext = in_pContext;

	// in_rFormat.channelConfig.eConfigType will be different than AK_ChannelConfigType_Objects if the configuration of the input of the plugin is known and does not support a 
    // dynamic number of objects. However this plug-in is pointless if it is not instantiated on an Audio Object bus, so we are better off letting our users know.
    if (in_rFormat.channelConfig.eConfigType != AK_ChannelConfigType_Objects)
        return AK_UnsupportedChannelConfig;
        
    // Inform the host that the output will be stereo. The host will create an output object for us and pass it to Execute.
    in_rFormat.channelConfig.SetStandard(AK_SPEAKER_SETUP_STEREO);

	return AK_Success;
}

AKRESULT BinauralObjectProcessorFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
	AK_PLUGIN_DELETE(in_pAllocator, this);
	return AK_Success;
}

AKRESULT BinauralObjectProcessorFX::Reset()
{
	return AK_Success;
}

AKRESULT BinauralObjectProcessorFX::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
	out_rPluginInfo.eType = AkPluginTypeEffect;
	out_rPluginInfo.bIsInPlace = false;
	out_rPluginInfo.bCanProcessObjects = true;
	out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
	return AK_Success;
}

void BinauralObjectProcessorFX::Execute(
		const AkAudioObjects& in_objects,	///< Input objects and object buffers.
		const AkAudioObjects& out_objects	///< Output objects and object buffers.
	)
{
    AKASSERT(in_objects.uNumObjects > 0); // Should never be called with 0 objects if this plug-in does not force tails.
    AKASSERT(out_objects.uNumObjects == 1); // Output config is a stereo channel stream.
    
    // "Binauralize" (just mix) objects in stereo output buffer.
    // For the purpose of this demonstration, instead of applying HRTF filters, let's call the built-in service to compute panning gains.
    
    // The output object should be stereo. Clear its two channels.
    AKASSERT(out_objects.ppObjectBuffers[0]->GetChannelConfig().uChannelMask == AK_SPEAKER_SETUP_STEREO);
    memset(out_objects.ppObjectBuffers[0]->GetChannel(0), 0, out_objects.ppObjectBuffers[0]->MaxFrames() * sizeof(AkReal32));
    memset(out_objects.ppObjectBuffers[0]->GetChannel(1), 0, out_objects.ppObjectBuffers[0]->MaxFrames() * sizeof(AkReal32));
    
    AKRESULT eState = AK_NoMoreData;
    
    for (AkUInt32 i = 0; i < in_objects.uNumObjects; ++i)
    {
        // State management: set the output to AK_DataReady as long as one of the inputs is AK_DataReady. Otherwise set to AK_NoMoreData.
        if (in_objects.ppObjectBuffers[i]->eState != AK_NoMoreData)
            eState = in_objects.ppObjectBuffers[i]->eState;
        
        // Prepare mixing matrix for this input.
        const AkUInt32 uNumChannelsIn = in_objects.ppObjectBuffers[i]->NumChannels();
        AkUInt32 uTransmixSize = AK::SpeakerVolumes::Matrix::GetRequiredSize(
            uNumChannelsIn,
            2);
        AK::SpeakerVolumes::MatrixPtr mx = (AK::SpeakerVolumes::MatrixPtr)AkAllocaSIMD(uTransmixSize);
        AK::SpeakerVolumes::Matrix::Zero(mx, uNumChannelsIn, 2);
        
        // Compute panning gains and fill the mixing matrix.
        m_pContext->GetMixerCtx()->ComputePositioning(
            in_objects.ppObjects[i]->positioning,
            in_objects.ppObjectBuffers[i]->GetChannelConfig(),
            out_objects.ppObjectBuffers[0]->GetChannelConfig(),
            mx
        );
        
        // Using the mixing matrix, mix the channels of the ith input object into the one and only stereo output object.
        AK_GET_PLUGIN_SERVICE_MIXER(m_pContext->GlobalContext())->MixNinNChannels(
            in_objects.ppObjectBuffers[i],
            out_objects.ppObjectBuffers[0],
            1.f,
            1.f,
            mx, /// NOTE: To properly interpolate from frame to frame and avoid any glitch, we would need to store the previous matrix (OR positional information) for each object.
            mx);
    }
    
    // Set the output object's state.
    out_objects.ppObjectBuffers[0]->uValidFrames = in_objects.ppObjectBuffers[0]->MaxFrames();
    out_objects.ppObjectBuffers[0]->eState = eState;
}
