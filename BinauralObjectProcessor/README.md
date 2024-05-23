## <Binaural Object Processor>

The Binaural Object Processor takes in multiple Audio Objects, and outputs a single Audio Object with a stereo channel configuration. Such a plug-in should be mounted on an Audio Object bus, but it will effectively make this bus output a single stereo signal. It falls into the category of `Out-of-place Object Processors` that manage distinct sets of Audio Objects at their input and output. The input Audio Objects depend on the host bus, while the output Audio Objects are created explicitly by the plug-in.

The main purpose of this plugin is to provide flexibility and access to more low-level operations through the Wwise's SDK for 3D/Spatial audio.
This is currently a default implementation of the Binauralizer as described in [Wwise's documentation](https://www.audiokinetic.com/en/library/edge/?source=SDK&id=soundengine_plugins_objectprocessor.html).
The intention is to use this as a template and expand it for EVE's specific use cases.


