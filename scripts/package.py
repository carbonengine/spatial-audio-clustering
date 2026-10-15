# Copyright 2026 CCP ehf.
#
# This software was developed by CCP Games for spatial audio object clustering
# in EVE Online and EVE Frontier.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# This license does not grant any rights to CCP's trademarks or game content.
# EVE Online and EVE Frontier are registered trademarks of CCP ehf.

"""Builds the Spatial Clustering plug-in and packages it as a Wwise Launcher bundle.

Run it from a Developer Command Prompt for VS 2026, with WWISEROOT set to the Wwise
install to build against:

    python scripts/package.py               build everything, then package
    python scripts/package.py --skip-build  package what is already built

The result is dist/SpatialClustering_<version>_Wwise<year>.<major>/ and a zip of it.
Users install it with Wwise Launcher > Plug-ins > Add from directory, pointing at the
bundle folder inside.

wp.py writes its build outputs into the Wwise install itself, as for any plug-in build.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PLUGIN_NAME = "SpatialClustering"
REPO_ROOT = Path(__file__).resolve().parent.parent
WWISE_PROJECT = REPO_ROOT / "wwise"
DIST_DIR = REPO_ROOT / "dist"
VERSION_HEADER = WWISE_PROJECT / "SpatialClusteringVersion.h"

# (wp.py platform, toolset, configurations). The authoring plug-in only ships in Release,
# since generate-bundle marks Debug authoring packages as needing a Wwise debug license.
BUILDS = [
    ("Windows_vc180", "vc180", ["Debug", "Profile", "Release"]),
    ("Authoring", "vc170", ["Release"]),
]
PACKAGES = ["Common", "Windows_vc180", "Authoring", "Documentation"]
EXTRA_FILES = ["LICENSE.txt", "NOTICE", "CHANGELOG.md"]


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def read_defines(path, names):
    """Returns the integer value of each #define in names, read from a header."""
    text = path.read_text(encoding="utf-8")
    values = []
    for name in names:
        match = re.search(rf"^#define\s+{name}\s+(\d+)\s*$", text, re.MULTILINE)
        if not match:
            fail(f"{name} is not defined in {path}")
        values.append(int(match.group(1)))
    return values


def run_wp(wp_py, *args):
    command = [sys.executable, str(wp_py), *args]
    print(f"\n> wp.py {' '.join(args)}", flush=True)
    result = subprocess.run(command, cwd=WWISE_PROJECT)
    if result.returncode != 0:
        fail(f"wp.py {' '.join(args)} failed with exit code {result.returncode}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--skip-build", action="store_true", help="package the plug-in that is already built")
    args = parser.parse_args()

    if "WWISEROOT" not in os.environ:
        fail("WWISEROOT is not set; set it to the Wwise install to build against")
    wwise_root = Path(os.environ["WWISEROOT"])
    wp_py = wwise_root / "Scripts" / "Build" / "Plugins" / "wp.py"
    if not wp_py.is_file():
        fail(f"{wp_py} does not exist")

    major, minor, patch = read_defines(
        VERSION_HEADER,
        ["SPATIALCLUSTERING_VERSION_MAJOR", "SPATIALCLUSTERING_VERSION_MINOR", "SPATIALCLUSTERING_VERSION_PATCH"])
    if minor > 99:
        fail("the minor version must be below 100 to fit the bundle version")
    wwise_year, wwise_major = read_defines(
        wwise_root / "SDK" / "include" / "AK" / "AkWwiseSDKVersion.h",
        ["AK_WWISESDK_VERSION_MAJOR", "AK_WWISESDK_VERSION_MINOR"])

    # The Launcher reads year.major of the bundle version as the Wwise version the bundle targets,
    # so the plug-in version goes in the last two numbers: 3.1.2 on Wwise 2025.1 is 2025.1.301.2.
    plugin_version = f"{major}.{minor}.{patch}"
    bundle_version = f"{wwise_year}.{wwise_major}.{major * 100 + minor}.{patch}"
    print(f"Spatial Clustering {plugin_version} for Wwise {wwise_year}.{wwise_major} (bundle version {bundle_version})")

    if not args.skip_build:
        for platform, toolset, configurations in BUILDS:
            run_wp(wp_py, "premake", platform, "-t", toolset)
            for configuration in configurations:
                run_wp(wp_py, "build", platform, "-c", configuration, "-x", "x64", "-t", toolset)
        run_wp(wp_py, "build", "Documentation")

    # generate-bundle lists every package in the folder, so remove those left by earlier runs.
    for stale in [*WWISE_PROJECT.glob(f"{PLUGIN_NAME}_*.tar.xz"), WWISE_PROJECT / "bundle.json"]:
        stale.unlink(missing_ok=True)

    for platform in PACKAGES:
        run_wp(wp_py, "package", platform, "--version", bundle_version)
    run_wp(wp_py, "generate-bundle", "--version", bundle_version)

    out_dir = DIST_DIR / f"{PLUGIN_NAME}_{plugin_version}_Wwise{wwise_year}.{wwise_major}"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    bundle_dir = out_dir / "bundle"
    bundle_dir.mkdir(parents=True)
    for package in [*WWISE_PROJECT.glob(f"{PLUGIN_NAME}_*.tar.xz"), WWISE_PROJECT / "bundle.json"]:
        shutil.move(str(package), bundle_dir / package.name)
    for name in EXTRA_FILES:
        shutil.copy2(REPO_ROOT / name, out_dir / name)
    archive = shutil.make_archive(str(out_dir), "zip", root_dir=DIST_DIR, base_dir=out_dir.name)

    print(f"\nBundle folder: {bundle_dir}")
    for item in sorted(bundle_dir.iterdir()):
        print(f"  {item.name}")
    print(f"Release zip:   {archive}")


if __name__ == "__main__":
    main()
