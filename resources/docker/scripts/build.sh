#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Building slate-service binary..."
cd "/slate/build"
cmake3 .. -DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=False -DSTATIC_CLIENT=False
make -k