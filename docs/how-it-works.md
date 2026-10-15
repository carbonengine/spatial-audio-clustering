# How it works

Spatial Clustering is an object processor. Unlike traditional Effect plug-ins, which are agnostic to Audio Objects, object processors have direct control over the Audio Objects (`AkAudioObject`) passing through a bus, so they can process their metadata and spatial characteristics.

Every audio frame the plug-in reads all audio objects on its bus, updates the clusters and writes one output object per cluster and one per passthrough input.

## Inputs

- An input can be clustered when it is mono, uses 3D spatialization (Position or Position + Orientation) and has Speaker Panning / 3D Spatialization Mix at 100.
- Every other input passes through: it gets its own output with the same channel configuration, audio, positioning, custom metadata and priority. This covers stereo and multichannel ambiences, beds, 2D sounds and partly spatialized sounds.
- Inputs only share a cluster when they carry the same set of custom metadata plug-in types; a cluster output carries the custom metadata of its representative input.

## Direction from the listener

On an Audio Objects bus, object positions are relative to the listener. The plug-in compares the directions of the inputs, so "nearby" means "within a given angle as heard by the listener". With **Distance Weight** above 0, a difference in distance is added to the angle on a log scale: at 1, a doubling of distance counts as much as the **Join Angle**.

Inputs at the listener position (closer than 0.0001 game units), such as sounds on the listener's own game object, have no direction. They share clusters only with each other.

## Joining and leaving

- A new input joins the nearest cluster within the **Join Angle**. If there is none, it starts a new cluster.
- When the **Max Outputs** limit is reached, a new input with no cluster within the **Join Angle** joins the nearest cluster within **Join Angle** × **Leave Ratio**. If there is none either, it still starts a new cluster, and the output budget merges the closest pair of clusters in the same frame.
- An input leaves its cluster only after it has been farther than **Join Angle** × **Leave Ratio** from the cluster for the **Dwell Time**. The gap between the two angles and the waiting time stop inputs from switching back and forth.
- A leaving input joins another cluster within the **Join Angle**, or starts its own cluster. While the **Max Outputs** limit is reached and no other cluster is within the **Join Angle**, it stays in its cluster.
- An input whose custom metadata plug-in types change leaves its cluster right away, with a crossfade, unless it is the only input in the cluster.
- A cluster with a single input follows that input and never loses it.

## Crossfaded moves

When an input moves to another cluster, its gain in the old cluster ramps down while its gain in the new cluster ramps up over the **Crossfade Time**. The two gains always add up to 1, so the input keeps its level during the move.

A switch between clustered and passthrough, for example when an RTPC moves Speaker Panning / 3D Spatialization Mix below 100, happens without a crossfade.

## Merging

Two clusters that stay within the **Join Angle** of each other for the **Dwell Time** merge. The lighter cluster fades into the other one; with **Loudness Weighting** on, the lighter cluster is the quieter one, otherwise it is the one with fewer inputs. A merge only happens if the merged cluster would still fit within the **Join Angle**.

## Output budget

With **Max Outputs** above 0, the plug-in counts clusters plus passthrough outputs. When the count is over the limit, it merges the closest pairs of clusters right away, without waiting for the dwell time. A higher priority makes a pair count as farther apart, so it merges later.

- At the limit, a new input that is farther than **Join Angle** × **Leave Ratio** from every cluster gets a cluster of its own, and the closest pair of clusters merges instead. A sound far from all others is therefore not pulled into a distant cluster.
- Passthrough inputs, excluded inputs and clusters with different custom metadata cannot be merged. If the limit cannot be reached, the plug-in posts a message to the Capture Log.
- A cluster that has been merged keeps its output until its crossfade ends, so the number of outputs can be above the limit for the length of a crossfade.

## Cluster outputs

- **Position**: the average direction of the contributing inputs, at their average distance. With **Loudness Weighting** on, each input counts by its RMS level, smoothed over about 50 ms.
- **One contributing input**: the output copies the input's positioning.
- **Several contributing inputs (including inputs fading in or out)**: the output uses Position spatialization at the cluster position with the default orientation. Its spread is the larger of the spread of the representative input (the loudest one when **Loudness Weighting** is on, otherwise the first input at full gain) and **Spread From Cluster Size** × (cluster angular radius / 180°).
- **Priority**: the highest priority among the contributing inputs.
- **Custom metadata**: copied from the representative input.
- **Name**: cluster outputs appear as `Cluster <id>` in the profiler. Passthrough outputs are named `Passthrough`, or keep the name an upstream object processor gave their input.

## Exclusions

Inputs with a priority above **Exclude Priority Above**, and inputs closer to the listener than **Near-Field Radius**, keep an output of their own and are never merged. An input that becomes excluded while it is in a cluster leaves it after the Dwell Time, with a crossfade.

## Performance

Memory comes from the Wwise plug-in allocator. It is reserved for 64 inputs in Init and grows only when more inputs arrive. An update for 512 inputs takes about 30 to 60 µs in the [benchmark](building.md#testing-without-a-game). Lowering Max Outputs far below the current number of clusters merges them all in one update, which takes about 0.7 to 1.4 ms for 512 inputs and 1.5 to 3.5 ms for 1024 inputs in the same benchmark.
