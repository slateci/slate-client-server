#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Build slate-service:
cd "/work/build"
echo "CMAKE3 options: -DBUILD_CLIENT=${DBUILD_CLIENT} -DBUILD_SERVER=${DBUILD_SERVER} -DBUILD_SERVER_TESTS=${DBUILD_SERVER_TESTS} -DSTATIC_CLIENT=${DSTATIC_CLIENT}"
cmake3 .. -DBUILD_CLIENT=${DBUILD_CLIENT} -DBUILD_SERVER=${DBUILD_SERVER} -DBUILD_SERVER_TESTS=${DBUILD_SERVER_TESTS} -DSTATIC_CLIENT=${DSTATIC_CLIENT}
make
cp /work/build/slate-service /usr/bin/slate-service

# Start slate-service:
${1:-/usr/bin/slate-service}