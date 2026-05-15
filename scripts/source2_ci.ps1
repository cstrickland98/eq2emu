param(
  [string]$BuildDir = "build/source2",
  [string]$Config = "Debug",
  [switch]$LiveSmoke
)

$ErrorActionPreference = "Stop"

$liveSmokeValue = if ($LiveSmoke.IsPresent) { "ON" } else { "OFF" }

$configureArgs = @(
  "-S", ".",
  "-B", $BuildDir,
  "-DEQ2EMU_BUILD_SOURCE2=ON",
  "-DEQ2_SOURCE2_BUILD_APPS=ON",
  "-DEQ2_SOURCE2_BUILD_TOOLS=ON",
  "-DEQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS=$liveSmokeValue"
)

cmake @configureArgs

cmake --build $BuildDir --config $Config
ctest --test-dir $BuildDir -C $Config --output-on-failure
