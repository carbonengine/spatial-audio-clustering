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
Here is how to generate the solutions for Authoring on the current operating system:

`cd MyNewFX`

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" premake Authoring`

To see the currently available platforms on your system run:

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" premake --help`

Once a project solution has been created you can generate binaries using the build command.
Example:

`python "%WWISEROOT%/Scripts/Build/Plugins/wp.py" build Windows_vc160 -c Release`

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





