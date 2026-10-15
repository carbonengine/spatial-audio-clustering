## Loudness Weighting

When on, each input counts by its RMS level, smoothed over about 50 ms, so clusters move toward their louder inputs. When two clusters merge, the quieter one fades into the other. The loudest input is the representative whose spread and custom metadata the output uses.

When off, every input counts the same. When two clusters merge, the one with fewer inputs fades into the other, and the representative is the first input at full gain.

**Note**: This is the only parameter that cannot be driven by an RTPC.
