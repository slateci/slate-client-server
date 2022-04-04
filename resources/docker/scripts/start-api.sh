#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Building slate-service binary..."
cd "${SLATE_VOLUME_DIR}/build"
cmake3 .. -DBUILD_CLIENT=False -DBUILD_SERVER=True -DBUILD_SERVER_TESTS=True -DSTATIC_CLIENT=False
make
cp "${SLATE_VOLUME_DIR}/build/slate-service" /usr/bin/slate-service

echo "Starting slate-service..."
slate-service \
--allowAdHocApps True \
--bootstrapUserFile "${SLATE_VOLUME_DIR}/resources/docker/users/slate_portal_user" \
--config "${SLATE_VOLUME_DIR}/resources/docker/conf/slate.conf"