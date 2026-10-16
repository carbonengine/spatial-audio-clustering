## Crossfade Time

Time over which an input fades out of its old cluster and into its new one when it moves. The two gains always add up to 1, so the input keeps its level during the move. When two clusters merge, the quieter one fades into the louder one over the same time.

**Note**: The time is rounded to whole audio frames, and a crossfade always lasts at least one frame. A switch between clustered and passthrough happens without a crossfade.

Units: milliseconds
