#include "DSPUtilities.h"
#include <algorithm>
#include <cstring>
#include <cmath>

DSPUtilities::DSPUtilities() = default;

DSPUtilities::~DSPUtilities() = default;

void DSPUtilities::ClearBuffers(AkAudioObjects& audioObjects)
{
    for (AkUInt32 i = 0; i < audioObjects.uNumObjects; ++i) {
        AkAudioBuffer* pBuffer = audioObjects.ppObjectBuffers[i];
        for (AkUInt32 j = 0; j < pBuffer->NumChannels(); ++j) {
            // Clear the buffer to zero
            std::fill(pBuffer->GetChannel(j), pBuffer->GetChannel(j) + pBuffer->MaxFrames(), 0.0f);
        }
    }
}

void DSPUtilities::CopyBuffer(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer)
{
    for (AkUInt32 j = 0; j < inBuffer->NumChannels(); ++j)
    {
        AkReal32* pInBuf = inBuffer->GetChannel(j);
        AkReal32* outBuf = outBuffer->GetChannel(j);
        memcpy(outBuf, pInBuf, inBuffer->uValidFrames * sizeof(AkReal32));
    }
}

AkAudioObjectID DSPUtilities::CreateOutputObject(const AkAudioObject* inobj, const AkAudioObjects& inObjects, const AkUInt32 index, AK::IAkEffectPluginContext* m_pContext)
{
    AkAudioObjectID outputObjKey = AK_INVALID_AUDIO_OBJECT_ID;

    AkUInt32 numObjsOut = 1;

    // Allocate space for a new output object
    AkAudioObject* newObject = (AkAudioObject*)AkAlloca(sizeof(AkAudioObject*));
    AkAudioObjects outputObjects;
    outputObjects.uNumObjects = numObjsOut;
    outputObjects.ppObjectBuffers = nullptr;
    outputObjects.ppObjects = &newObject;

    if (m_pContext->CreateOutputObjects(inObjects.ppObjectBuffers[index]->GetChannelConfig(), outputObjects) == AK_Success)
    {
        AkAudioObject* pObject = newObject;
        outputObjKey = pObject->key;

        pObject->CopyContents(*inobj);
        pObject->positioning.behavioral.spatMode = inobj->positioning.behavioral.spatMode;
        pObject->positioning.threeD = inobj->positioning.threeD;
    }

    return outputObjKey;
}

AKRESULT DSPUtilities::AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, const AkUInt32 in_uNumChannelsIn, const AkUInt32 in_uNumChannelsOut)
{
    AkUInt32 size = AK::SpeakerVolumes::Matrix::GetRequiredSize(in_uNumChannelsIn, in_uNumChannelsOut);
#if _WIN32
    volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)_aligned_malloc(size, AK_SIMD_ALIGNMENT);
#else
    volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)std::aligned_alloc(AK_SIMD_ALIGNMENT, size);
#endif
    return volumeMatrix ? AK_Success : AK_InsufficientMemory;
}

void DSPUtilities::ApplyWwiseMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes, GeneratedObjects* pGeneratedObject, AK::IAkEffectPluginContext* m_pContext)
{
    AkUInt32 uTransmixSize = AK::SpeakerVolumes::Matrix::GetRequiredSize(inBuffer->NumChannels(), outBuffer->NumChannels());

    // If mixVolumes doesn't exist, allocate and set it to the current volumes
    if (pGeneratedObject->volumeMatrix == nullptr) {
        AKRESULT eResult = AllocateVolumes(pGeneratedObject->volumeMatrix, inBuffer->NumChannels(), outBuffer->NumChannels());
        if (eResult == AK_Success) {
            AKPLATFORM::AkMemCpy(pGeneratedObject->volumeMatrix, currentVolumes, uTransmixSize);
        }
    }

    AK_GET_PLUGIN_SERVICE_MIXER(m_pContext->GlobalContext())->MixNinNChannels(
        inBuffer,
        outBuffer,
        cumulativeGain.fPrev,
        cumulativeGain.fNext,
        pGeneratedObject->volumeMatrix,
        currentVolumes
    );
    // Update stored volumes
    AKPLATFORM::AkMemCpy(pGeneratedObject->volumeMatrix, currentVolumes, uTransmixSize);
}

void DSPUtilities::ApplyCustomMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes)
{
    if (inBuffer->uValidFrames == 0 || inBuffer->NumChannels() == 0 || outBuffer->NumChannels() == 0) {
        return;
    }

    // Calculate the gain increment per sample
    AkReal32 fOneOverNumFrames = 1.f / (AkReal32)inBuffer->uValidFrames;
    AkReal32 gainIncrement = (cumulativeGain.fNext - cumulativeGain.fPrev) * fOneOverNumFrames;

    // Iterate through all frames
    for (AkUInt32 frame = 0; frame < inBuffer->uValidFrames; ++frame) {
        // Do some manual gain compensation and create a ramp
        AkReal32 currentGain = cumulativeGain.fPrev + gainIncrement * frame;
        //Iterate for every channel and mix the input to the output
        AkReal32* inSamples = inBuffer->GetChannel(0) + frame;
        for (AkUInt32 inChan = 0; inChan < inBuffer->NumChannels(); ++inChan, inSamples += inBuffer->uValidFrames) {
            //Get the current input sample
            AkReal32 inSample = *inSamples;
            AkReal32* outSamples = outBuffer->GetChannel(0) + frame;
            for (AkUInt32 outChan = 0; outChan < outBuffer->NumChannels(); ++outChan, outSamples += outBuffer->uValidFrames) {
                // The actual mixing inSample multiplied by the current gain
                *outSamples += inSample * currentGain * currentVolumes[inChan * outBuffer->NumChannels() + outChan];
            }
        }
    }

    // Make sure that the output buffer has a valid frame count
    outBuffer->uValidFrames = AkMin(outBuffer->MaxFrames(), inBuffer->uValidFrames);
    NormalizeBuffer(outBuffer);
}

void DSPUtilities::NormalizeBuffer(AkAudioBuffer* pBuffer)
{
    if (pBuffer->uValidFrames == 0 || pBuffer->NumChannels() == 0) {
        return;
    }

    // Find the maximum absolute sample value across all channels
    AkReal32 maxSample = 0.0f;
    AkUInt32 numChannels = pBuffer->NumChannels();
    AkUInt32 validFrames = pBuffer->uValidFrames;

    for (AkUInt32 chan = 0; chan < numChannels; ++chan) {
        AkReal32* pChannel = pBuffer->GetChannel(chan);
        for (AkUInt32 frame = 0; frame < validFrames; ++frame) {
            AkReal32 absSample = std::fabs(pChannel[frame]);
            if (absSample > maxSample) {
                maxSample = absSample;
            }
        }
    }

    // If the maximum sample is greater than 1.0, normalize the buffer
    if (maxSample > 1.0f) {
        AkReal32 normalizationFactor = 1.0f / maxSample;
        for (AkUInt32 chan = 0; chan < numChannels; ++chan) {
            AkReal32* pChannel = pBuffer->GetChannel(chan);
            for (AkUInt32 frame = 0; frame < validFrames; ++frame) {
                pChannel[frame] *= normalizationFactor;
            }
        }
    }
}


