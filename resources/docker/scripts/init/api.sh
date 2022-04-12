#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Building slate-service binary..."
cd "/slate/build"
cmake3 .. -DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False
make

#. /slate/resources/docker/scripts/ctest.sh

echo "Starting slate-service..."
ln -sf /slate/build/slate-service /usr/bin/slate-service
slate-service \
--config "/slate/resources/docker/conf/slate.conf"
