# This script is just a template and has to be copied and modified per project
# This script should be called from .vscode/tasks.json with
#
#   scripts/Build-Release.ps1            - for Beta builds
#   scripts/Build-Release.ps1 Release    - for Release builds
#
# {
#     "label": "Build-Release",
#     "type": "shell",
#     "command": "scripts/Build-Release.ps1 Release",
#     "args": [],
#     "problemMatcher": [],
#     "group": "test"
# },
# {
#     "label": "Build-Beta",
#     "type": "shell",
#     "command": "scripts/Build-Release.ps1 ",
#     "args": [],
#     "problemMatcher": [],
#     "group": "test"
# }



# set product names, allows mapping of (devel) name in Project to a more consistent name in release
# $settings = scripts/OpenKNX-Build-Settings.ps1

# # execute generic pre-build steps
# lib/OGM-Common/scripts/setup/reusable/Build-Release-Preprocess.ps1 $args[0]
# if (!$?) { exit 1 }

# # build firmware based on generated headerfile 

# # Example call, the following 2 lines might be there multiple times for each firmware which should be built
# lib/OGM-Common/scripts/setup/reusable/Build-Step.ps1 release_RP2040 firmware uf2 GardenControl
# if (!$?) { exit 1 }

# # execute generic post-build steps
# lib/OGM-Common/scripts/setup/reusable/Build-Release-Postprocess.ps1 $args[0]
# if (!$?) { exit 1 }


# This script is just a template and has to be copied and modified per project
# This script should be called from .vscode/tasks.json with
#
#   scripts/Build-Release.ps1            - for Beta builds
#   scripts/Build-Release.ps1 Release    - for Release builds
#
# {
#     "label": "Build-Release",
#     "type": "shell",
#     "command": "scripts/Build-Release.ps1 Release",
#     "args": [],
#     "problemMatcher": [],
#     "group": "test"
# },
# {
#     "label": "Build-Beta",
#     "type": "shell",
#     "command": "scripts/Build-Release.ps1 ",
#     "args": [],
#     "problemMatcher": [],
#     "group": "test"
# }

# set product names, allows mapping of (devel) name in Project to a more consistent name in release
# $settings = scripts/OpenKNX-Build-Settings.ps1 $args[0]
# $releaseIndication = $settings["appRelease"]

# execute generic pre-build steps
lib/OGM-Common/scripts/setup/reusable/Build-Release-Preprocess.ps1 $args[0]
if (!$?) { exit 1 }

if (Test-Path -Path scripts/data -PathType Container) {
    Copy-Item scripts/data/* release/data/
    if (!$?) { exit 1 }
}

# Example call, the following 2 lines might be there multiple times for each firmware which should be built
lib/OGM-Common/scripts/setup/reusable/Build-Step.ps1 release_RP2040 firmware-SmartMF-GardenControl rp2040-tp
if (!$?) { exit 1 }



# Remove unnecessary OTA upload scripts
Remove-Item -Path release/OTA-Upload-Firmware-*.ps1 -ErrorAction SilentlyContinue -Force


# execute generic post-build steps
lib/OGM-Common/scripts/setup/reusable/Build-Release-Postprocess.ps1 $args[0]
if (!$?) { exit 1 }

if (Test-Path -Path release-collection -PathType Container) {
    Copy-Item release/* release-collection/
}
