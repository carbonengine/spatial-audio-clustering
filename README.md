# ccp-wwise-audio-plugins
This repository contains custom audio plugins for Wwise, developed by CCP.


### Quick Set-Up Guide

To build the property help documentation, install these two Python dependencies: 

`pip install markdown`
`pip install jinja2`

To create a new plug-in, run the following commands and answer the prompts:

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" new`

This will create a directory that is named after your plug-in's current working directory.

Once the plug-in project has been created, other commands, such as premake, must be called from within the project folder.
Here is how to generate the solutions for the available platforms.
Example:

`cd MyNewFX`

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" premake Windows_vc160`

To see the currently available platforms on your system run:

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" premake --help`

Once a project solution has been created you can generate binaries using the build command.
Example:

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" build Windows_vc160 -c Debug`

This will output the binaries directly to your Wwise installation and will be ready for testing.

For a more in-depth documentation for all the above, please visit  Wwise's documenation for [Using the Development Tools](https://www.audiokinetic.com/en/library/edge/?source=SDK&id=effectplugin_tools.html).

### Requirments

To build Authoring and Engine plugins on Windows you’ll need:

- If using Visual Studio 2019:
    - Desktop development with C++ workload
    - MSVC v142 - VS 2019 C++ x64/x86 build tools
    - C++ ATL for latest v142 build tools (x86 & x64)
    - C++ MFC for latest v142 build tools (x86 & x64)
    - Windows Universal CRT SDK
    - Windows 10 SDK (10.0.19041.0)
        - Different version can be specified in a generated PremakePlugin.lua file
- If using Visual Studio 2022
    - Requirements are the same as for 2019, but components of version v143 must be installed instead
    Note: Wwise of version at least 2022.1.5 is required to build plugins with this version of Visual Studio
- Wwise 2022 or later
    - Version 2021 should work, too, but it wasn't tested
    - SDKs with required deployment platforms must be installed through Wwise Launcher


# Debugging

## Setting the Right Configuration and Optimization Level

After you have run "wp.py premake Authoring", you should have the Visual Studio projects and solutions for Wwise Authoring. (refer to Using the Development Tools)
Open the solution ending in "_Authoring_Windows_vc1X0.sln" that corresponds to the Visual Studio studio version you want to use (vc140 = 2015, vc150 = 2017, vc160 = 2019).

Once in Visual Studio, select "Release" and "x64" as the target configuration. You cannot use the Debug target for Authoring as it expects to build against debug libraries, which you likely do not have access to.
Note how DebugExampleFX (the Sound Engine plug-in) has configuration "Profile" for Authoring: this is normal.

Because optimization can impact your capacity to debug, you can temporarily set the optimization level to /0d (disabled) while you develop for the Release configuration.
Simply Right-Click on the Visual Studio project you want to set, then "Properties" to access the Project Property Pages.
Under C/C++ > Optimization, set the Optimization Level to /0d (disabled).

If you build the Solution (Ctrl+Shift+B), the plug-in should be built inside the Wwise installation directory you used wp.py from (which should correspond to your WWISEROOT environment variable).
This will therefore put your plug-in under %WWISEROOT%/Authoring/x64/Release/bin/Plugins.

## Attach to the running Process

To debug the plug-in when running inside Wwise Authoring, create a new project and add the plug-in somewhere, e.g., for an Effect add it in the Effects tab of a Sound SFX.
Then, set a breakpoint in Visual Studio in the Execute function of your plug-in (located in the <PluginName>FX.cpp file if you used the wp.py template).
Finally, select Debug > Attach to Process..., and find the Wwise.exe instance you want to debug. Once attached, play the Sound SFX and you should break inside Visual Studio!

To debug the Authoring part of the plug-in the process is exactly the same. You can also set the optimization level to /0d (disabled) for the Authoring project (the first in the solution explorer).





