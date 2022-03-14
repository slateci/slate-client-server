#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Building:
cd /work/build
echo "Using the cmake3 options: ${CMAKE_OPTS}..."
cmake3 .. ${CMAKE_OPTS}

${1:-/usr/bin/make}
