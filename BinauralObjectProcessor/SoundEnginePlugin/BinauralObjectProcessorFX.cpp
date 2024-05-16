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

AKRESULT BinauralObjectProcessorFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
	m_pParams = (BinauralObjectProcessorFXParams*)in_pParams;
	m_pAllocator = in_pAllocator;
	m_pContext = in_pContext;

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
	AkUInt16 uMaxFramesProcessed = 0;
	for (AkUInt32 objIdx = 0; objIdx < in_objects.uNumObjects; ++objIdx)
	{
		const AkUInt32 uNumChannels = in_objects.ppObjectBuffers[objIdx]->NumChannels();

		AkUInt16 uFramesProcessed;
		for (AkUInt32 i = 0; i < uNumChannels; ++i)
		{
			// Peek into object metadata if desired.
			AkAudioObject* pObject = in_objects.ppObjects[objIdx];
			
			AkReal32* AK_RESTRICT pBuf = (AkReal32* AK_RESTRICT)in_objects.ppObjectBuffers[objIdx]->GetChannel(i);

			uFramesProcessed = 0;
			while (uFramesProcessed < in_objects.ppObjectBuffers[objIdx]->uValidFrames)
			{
				// Get input object's samples
				++uFramesProcessed;
			}
			if (uFramesProcessed > uMaxFramesProcessed)
				uMaxFramesProcessed = uFramesProcessed;
		}
	}
	
	for (AkUInt32 objIdx = 0; objIdx < out_objects.uNumObjects; ++objIdx)
	{
		const AkUInt32 uNumChannels = out_objects.ppObjectBuffers[objIdx]->NumChannels();

		AkUInt16 uFramesProcessed;
		for (AkUInt32 i = 0; i < uNumChannels; ++i)
		{
			// Set output object's metadata if desired.
			AkAudioObject* pObject = out_objects.ppObjects[objIdx];
			
			AkReal32* AK_RESTRICT pBuf = (AkReal32* AK_RESTRICT)out_objects.ppObjectBuffers[objIdx]->GetChannel(i);

			uFramesProcessed = 0;
			while (uFramesProcessed < out_objects.ppObjectBuffers[objIdx]->uValidFrames)
			{
				// Fill output object's samples
				++uFramesProcessed;
			}
		}
		
		out_objects.ppObjectBuffers[objIdx]->uValidFrames = uMaxFramesProcessed;
		if (uMaxFramesProcessed > 0)
			out_objects.ppObjectBuffers[objIdx]->eState = AK_DataReady;
		else
			out_objects.ppObjectBuffers[objIdx]->eState = AK_NoMoreData;
	}
}
