#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Building:
echo "Building the slate server..."
cd /work/build
cmake3 ..
make

${1:-/bin/bash}