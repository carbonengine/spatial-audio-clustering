#pragma once

#include <AK/SoundEngine/Common/AkTypes.h>
#include <AK/SoundEngine/Common/AkCommonDefs.h>

struct GeneratedObjects
{
    bool isClustered = false;
    AkAudioObjectID uniqueClusterID = -1;
    AkAudioObjectID outputObjKey;
    int index;
    AK::SpeakerVolumes::MatrixPtr volumeMatrix = nullptr;
};
