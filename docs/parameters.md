# Parameters

All parameters can be driven by RTPCs. The Wwise property editor groups them like the table below, and its help panel has the same descriptions. Most scenes only need **Cluster Width**, and **Object Limit** when you have a fixed object budget.

| Group | Parameter | Range | Default | Description |
|---|---|---|---|---|
| Clustering | Cluster Width, degrees | 0 to 180 | 15 | Largest angle between an input and a cluster for the input to join it. 0 turns off merging by angle. |
| Clustering | Object Limit | 0 to 512 | 0 | Largest number of outputs, passthrough outputs included. 0 means no limit. |
| Exclusions | Priority Threshold | 0 to 100 | 100 | Inputs with a priority above this value are never merged. 100 excludes nothing. |
| Exclusions | Proximity Radius, game units | 0 to 1,000,000 | 0 | Inputs closer to the listener than this are never merged. The slider goes to 1000; type larger values. |
| Advanced | Hold Time, ms | 0 to 5000 | 200 | Time an input must stay outside its cluster before it leaves, and time two clusters must stay close before they merge. |
| Advanced | Crossfade Time, ms | 0 to 1000 | 50 | Time over which a moving input fades between clusters. |
| Advanced | Distance Sensitivity | 0 to 1 | 0 | How much a difference in distance counts against the Cluster Width. 0 uses the angle only. |
| Advanced | Cluster Spread | 0 to 1 | 1 | Share of the cluster's angular size used as its minimum spread. |

Hold Time and Crossfade Time are rounded to whole audio frames. At 48 kHz with 1024-sample buffers, one frame is about 21 ms. A crossfade always lasts at least one frame.

The same controls cover the whole range. A Cluster Width of 180 with the Object Limit at 1 mixes all clusterable inputs into one object, as long as no passthrough, excluded or listener-position inputs need outputs of their own and all inputs carry the same custom metadata. A Cluster Width of 0 with a Object Limit limit only merges when the limit is reached.

See [How it works](how-it-works.md) for what each parameter does in detail.
