#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Building slate-service binary..."
cd "/slate/build"
cmake3 .. -DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False
make

echo "Starting slate-service..."
/slate/build/slate-service \
--allowAdHocApps True \
--config "/slate/resources/docker/conf/slate.conf"
