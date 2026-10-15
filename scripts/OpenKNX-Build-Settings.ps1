
# set product names, allows mapping of (devel) name in Project to a more consistent name in release
$settings = lib/OGM-Common/scripts/build/OpenKNX-Build-Settings.ps1 $args[0] "GardenControl"

# hier wird auf "Beta" verglichen, weil ein arg[0] aufruf automatische beta dran hängt, wenn das gardencontrol.xml so heißt und nicht gardencontrol-release.xml
if ($settings.appRelease -eq "Beta") 
{
    $settings.appRelease = "Release"
}

Return $settings