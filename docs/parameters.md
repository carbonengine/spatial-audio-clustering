# Parameters

All parameters except Loudness Weighting can be driven by RTPCs. The same descriptions are in the help panel of the Wwise property editor.

| Parameter | Range | Default | Description |
|---|---|---|---|
| Join Angle (degrees) | 0 to 180 | 5 | Largest angle between an input and a cluster for the input to join it. 0 turns off merging by angle. |
| Leave Ratio (x Join Angle) | 1 to 4 | 1.5 | Angle at which an input starts to leave its cluster, as a multiple of the Join Angle. |
| Dwell Time (ms) | 0 to 5000 | 200 | Time an input must stay outside its cluster before it leaves, and time two clusters must stay close before they merge. |
| Crossfade Time (ms) | 0 to 1000 | 50 | Time over which a moving input fades between clusters. |
| Max Outputs (0 = no limit) | 0 to 512 | 0 | Largest number of outputs, passthrough outputs included. 0 means no limit. |
| Near-Field Radius (game units) | 0 to 1,000,000 | 0 | Inputs closer to the listener than this are never merged. The slider goes to 1000; type larger values. |
| Distance Weight | 0 to 1 | 0 | How much a difference in distance counts against the Join Angle. 0 uses the angle only. |
| Exclude Priority Above | 0 to 100 | 100 | Inputs with a priority above this value are never merged. 100 excludes nothing. |
| Spread From Cluster Size | 0 to 1 | 1 | Share of the cluster's angular size used as its minimum spread. |
| Loudness Weighting | on or off | on | Moves each cluster toward its louder inputs. |

Dwell Time and Crossfade Time are rounded to whole audio frames. At 48 kHz with 1024-sample buffers, one frame is about 21 ms. A crossfade always lasts at least one frame.

The same controls cover the whole range: a Join Angle of 180 with Max Outputs at 1 mixes all clusterable inputs into one object, as long as no passthrough or excluded inputs need outputs of their own and all inputs carry the same custom metadata. A Join Angle of 0 with a Max Outputs limit only merges when the limit is reached.

See [How it works](how-it-works.md) for what each parameter does to the clusters.
