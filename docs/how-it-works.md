# How It Works

Spatial Clustering is an object processor. Unlike traditional Effect plug-ins, which are agnostic to Audio Objects, object processors have direct control over the Audio Objects (`AkAudioObject`) passing through a bus, so they can process their metadata and spatial characteristics.

Every audio frame the plug-in reads all audio objects on its bus, updates the clusters and writes one output object per cluster and one per passthrough input.

## Inputs

- An input can be clustered when it has one or two channels, uses 3D spatialization (Position or Position + Orientation) and has Speaker Panning / 3D Spatialization Mix at 100.
- Cluster outputs are mono. A stereo input is mixed down with each channel at -3 dB, the Wwise stereo to mono downmix.
- Every other input passes through: it gets its own output with the same channel configuration, audio, positioning, custom metadata and priority. This covers inputs with more than two channels such as ambiences and beds, 2D sounds and partly spatialized sounds.
- Inputs only share a cluster when they carry the same set of custom metadata plug-in types.

## Direction from the Listener

On an Audio Objects bus, object positions are relative to the listener. The plug-in compares the directions of the inputs, so "nearby" means "within a given angle as heard by the listener". With **Distance Sensitivity** above 0, a difference in distance is added to the angle on a log scale: at 1, a doubling of distance counts as much as the **Cluster Width**.

Inputs at the listener position, closer than 0.0001 game units, such as sounds on the listener's own game object, have no direction. They share clusters only with each other, and their clusters never merge with clusters that have a direction.

## Joining and Leaving

- A new input joins the nearest cluster within the **Cluster Width**. If there is none, it starts a new cluster.
- When the **Object Limit** is reached, a new input with no cluster within the **Cluster Width** joins the nearest cluster within 1.5 × the **Cluster Width**. If there is none either, it still starts a new cluster, and the output budget merges the closest pair of clusters in the same frame.
- An input leaves its cluster only after it has been farther than 1.5 × the **Cluster Width** from the cluster for the **Hold Time**. The gap between the two angles and the waiting time stop inputs from switching back and forth. With a Cluster Width of 120° or more, inputs never leave because of their direction.
- A leaving input joins another cluster within the **Cluster Width**, or starts its own cluster. While the **Object Limit** is reached and no other cluster is within the **Cluster Width**, it stays in its cluster and tries again after another Hold Time.
- An input whose custom metadata plug-in types change leaves its cluster right away, with a crossfade, unless it is the only input in the cluster.
- A cluster with a single input follows that input, and the input never leaves it. The cluster can still merge with another one.

## Crossfaded Moves

When an input moves to another cluster, its gain in the old cluster ramps down while its gain in the new cluster ramps up over the **Crossfade Time**. The two gains always add up to 1.

A switch between clustered and passthrough, for example when an RTPC moves Speaker Panning / 3D Spatialization Mix below 100, happens without a crossfade.

## Merging

When a cluster's nearest neighbor stays within the **Cluster Width** for the **Hold Time**, the two clusters merge. The quieter cluster fades into the louder one. A merge only happens if the merged cluster would still fit within the **Cluster Width**.

## Output Budget

With an **Object Limit** above 0, the plug-in counts clusters plus passthrough outputs. When the count is over the limit, it merges the closest pairs of clusters right away, without waiting for the Hold Time. A higher priority makes a pair count as farther apart, so it merges later.

- Budget merges have no angle limit, so a tight budget can produce wide clusters.
- At the limit, a new input that is farther than 1.5 × the **Cluster Width** from every cluster gets a cluster of its own, and the closest pair of clusters merges instead. A sound far from all others is therefore not pulled into a distant cluster.
- Passthrough inputs, excluded inputs, clusters at the listener position and clusters with different custom metadata cannot be merged. If the limit cannot be reached, the plug-in posts a message to the Capture Log.
- A cluster that has been merged keeps its output until the inputs moving out of it have faded, so the number of outputs can be above the limit for up to two crossfades.

## Cluster Outputs

- **Position**: the average direction of the contributing inputs, at their average distance. Each input counts by its RMS level, smoothed over about 50 ms, so a cluster sits closer to its louder inputs.
- **One contributing input**: the output copies the input's positioning.
- **Several contributing inputs, including inputs fading in or out**: the output uses Position spatialization at the cluster position with the default orientation. Its spread is the larger of the representative input's spread and **Cluster Spread** × cluster angular radius / 180°. The representative is the loudest input, and it can change from frame to frame.
- **Priority**: the highest priority among the contributing inputs.
- **Custom metadata**: copied from the representative input.
- **Name**: cluster outputs appear as `Cluster <id>` in the profiler. Passthrough outputs are named `Passthrough`, or keep the name an upstream object processor gave their input.

## Exclusions

Inputs with a priority above the **Priority Threshold**, inputs within the **Proximity Radius** of the listener and inputs with an invalid position get a cluster of their own that no other input joins and that never merges. Like every cluster, its output is mono. An input that becomes excluded while it is in a cluster leaves it after the Hold Time, with a crossfade.

## Performance

All memory comes from the Wwise plug-in allocator. Room for 64 inputs is reserved in Init, and it grows when more inputs or outputs are needed.

In the benchmark, an update for 512 inputs takes about 20 to 60 µs. Lowering the Object Limit far below the current number of clusters merges them all in one update, which takes about 0.7 to 1.6 ms for 512 inputs and 1.3 to 4 ms for 1024 inputs.
