#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j6
ctest --test-dir build --output-on-failure
xvfb-run -a pluginval --strictness-level 5 --validate-in-process --skip-gui-tests \
  --validate build/Glint_artefacts/Release/VST3/Glint.vst3
