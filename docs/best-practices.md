# Best practices

## Bus setup

- Insert the plug-in as an Effect on an Audio Objects bus (bus channel configuration set to Audio Objects). It cannot be inserted on sounds or containers and cannot be used for offline rendering.
- Use it on busses that carry many mono one-shots that are expensive to render as objects, such as weapon fire, impacts and debris.
- Multichannel inputs such as ambience loops always pass through untouched. If you set Max Outputs, route ambiences to another bus, since passthrough outputs count toward the limit.
- Keep important sounds, such as dialogue or the player's own weapons, out of clusters by giving them a priority above Exclude Priority Above, or by routing them to another bus.
- Use Near-Field Radius to keep sounds close to the listener separate.
- Use separate busses with their own settings for different categories, for example a smaller Join Angle for dense sources (turret fire) and a larger one for spread-out sounds (shields).

![Spatial Clustering inserted on an Audio Objects bus in Wwise](https://github.com/user-attachments/assets/88c217c1-87c9-4132-aba8-04eede2d46e1)

## Profiling

- In Audio Devices -> System monitor the system audio object consumption in real-time.
- In Views->Profiler->Audio Object List, select the bus. The Post-Effects stage shows the plug-in's outputs (`Cluster <id>` and the passthrough outputs); the Pre-Effects stage shows its inputs.
- Selections in the Audio Object List drive the Audio Object 3D Viewer, the Audio Object Meter and the Audio Object Metadata views. Use the 3D Viewer for a visual spatial representation of the clusters.
- Start a capture and check the Capture Log. The plug-in posts a message when it is not on an Audio Objects bus, when it needs more outputs than Max Outputs allows, when it cannot create output objects and when it runs out of memory. Repeated messages of the same kind are posted at most once every 5 seconds.

![Clusters in the Wwise Audio Object profiler views](https://github.com/user-attachments/assets/d46c84bb-196e-4c2a-b932-62920066b516)
