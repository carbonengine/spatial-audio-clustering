#include "Utilities.h"
#include <algorithm>
#include <cstring>
#include <cmath>

Utilities::Utilities() = default;

Utilities::~Utilities() = default;

void Utilities::ClearBuffers(AkAudioObjects& audioObjects)
{
    for (AkUInt32 i = 0; i < audioObjects.uNumObjects; ++i) {
        AkAudioBuffer* pBuffer = audioObjects.ppObjectBuffers[i];
        for (AkUInt32 j = 0; j < pBuffer->NumChannels(); ++j) {
            // Clear the buffer to zero
            std::fill(pBuffer->GetChannel(j), pBuffer->GetChannel(j) + pBuffer->MaxFrames(), 0.0f);
        }
    }
}

void Utilities::CopyBuffer(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer)
{
    for (AkUInt32 j = 0; j < inBuffer->NumChannels(); ++j)
    {
        AkReal32* pInBuf = inBuffer->GetChannel(j);
        AkReal32* outBuf = outBuffer->GetChannel(j);
        memcpy(outBuf, pInBuf, inBuffer->uValidFrames * sizeof(AkReal32));
    }
}

AkAudioObjectID Utilities::CreateOutputObject(const AkAudioObject* inobj, const AkAudioObjects& inObjects, const AkUInt32 index, AK::IAkEffectPluginContext* m_pContext, const AkVector* clusterPosition)
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

        pObject->positioning.behavioral.spatMode = inobj->positioning.behavioral.spatMode;
        pObject->positioning.threeD = inobj->positioning.threeD;

        // Set the position
        if (clusterPosition)
        {
            pObject->positioning.threeD.xform.SetPosition(*clusterPosition);
        }
        else
        {
            pObject->positioning.threeD.xform.SetPosition(inobj->positioning.threeD.xform.Position());
        }
    }
    return outputObjKey;
}

float Utilities::GetDistanceSquared(const AkVector& v1, const AkVector& v2)
{
    float dx = v1.X - v2.X;
    float dy = v1.Y - v2.Y;
    float dz = v1.Z - v2.Z;
    return dx * dx + dy * dy + dz * dz;
}

AkVector Utilities::CalculateMeanPosition(const std::vector<AkAudioObjectID>& clusterObjects, const AkAudioObjects& inObjects)
{
    AkVector sumPosition;
    sumPosition.X = 0.0f;
    sumPosition.Y = 0.0f;
    sumPosition.Z = 0.0f;
    int validObjectCount = 0;

    // Sum up positions of all objects in the cluster
    for (const auto& objId : clusterObjects) {
        for (AkUInt32 i = 0; i < inObjects.uNumObjects; i++) {
            if (inObjects.ppObjects[i]->key == objId) {
                const AkVector& pos = inObjects.ppObjects[i]->positioning.threeD.xform.Position();
                sumPosition.X += pos.X;
                sumPosition.Y += pos.Y;
                sumPosition.Z += pos.Z;
                validObjectCount++;
                break;
            }
        }
    }

    // Calculate mean position
    if (validObjectCount > 0) {
        sumPosition.X /= validObjectCount;
        sumPosition.Y /= validObjectCount;
        sumPosition.Z /= validObjectCount;
    }
    return sumPosition;
}


