# Best Practices

## Bus Setup

- Insert the plug-in as an Effect on an Audio Objects bus. It cannot be inserted on sounds or containers and cannot be used for offline rendering.
- Use it on busses that carry many one-shots that are expensive to render as objects, such as weapon fire, impacts and debris.
- Inputs with more than two channels, such as ambience loops, always pass through untouched. If you set an Object Limit, route ambiences to another bus, since passthrough outputs count toward the limit.
- Keep important sounds, such as dialogue or the player's own weapons, out of clusters by giving them a priority above the Priority Threshold, or by routing them to another bus.
- Use the Proximity Radius to keep sounds close to the listener separate.
- Use separate busses with their own settings for different categories, for example a smaller Cluster Width for dense sources such as turret fire and a larger one for spread-out sounds such as shields.

## Profiling

- In **Audio Devices > System**, monitor the system audio object consumption in real time.
- In **Views > Profiler > Audio Object List**, select the bus. The Post-Effects stage shows the plug-in's outputs, `Cluster <id>` and the passthrough outputs. The Pre-Effects stage shows its inputs.
- Selections in the Audio Object List drive the Audio Object 3D Viewer, the Audio Object Meter and the Audio Object Metadata views. Use the 3D Viewer to see the clusters in space.
- Start a capture and check the Capture Log. The plug-in posts a message when it is not on an Audio Objects bus, when it needs more outputs than the Object Limit allows, when it cannot create output objects and when it runs out of memory. The last three can repeat every frame, so each is posted at most once every 5 seconds.
