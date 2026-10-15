## Max Outputs

Largest number of output objects, passthrough outputs included. 0 means no limit.

When the count is over the limit, the closest pairs of clusters merge right away, without waiting for the Dwell Time. A higher priority makes a pair count as farther apart, so it merges later.

**Note**: Passthrough inputs, excluded inputs and clusters with different custom metadata cannot be merged. If the limit cannot be reached, the plug-in posts a message to the Capture Log. A merged cluster keeps its output until its crossfade ends, so the number of outputs can be above the limit for the length of a crossfade.

Units: output objects
