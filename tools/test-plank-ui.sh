#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"
if [[ ! -f managed_components/lvgl__lvgl/CMakeLists.txt ]]; then
    echo "Run the firmware gate first to resolve the pinned LVGL dependency." >&2
    exit 1
fi
cmake -S tests/ui -B build/plank-ui -G Ninja
cmake --build build/plank-ui --parallel 4
mkdir -p build/plank-ui/previews
build/plank-ui/test_plank_ui build/plank-ui/previews
build/plank-ui/test_plank_audio build/plank-ui/previews
if command -v sips >/dev/null 2>&1; then
    for preview in build/plank-ui/previews/*.ppm; do
        sips -s format png "$preview" --out "${preview%.ppm}.png" >/dev/null
    done
    install -m 0644 tools/plank-preview.html build/plank-ui/previews/index.html
    echo "Preview: build/plank-ui/previews/index.html"
fi
