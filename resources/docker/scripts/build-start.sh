#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Build slate-service:
cd "${SLATE_VOLUME_DIR}/build"
echo "Using cmake3 options: -DBUILD_CLIENT=${DBUILD_CLIENT} -DBUILD_SERVER=${DBUILD_SERVER} -DBUILD_SERVER_TESTS=${DBUILD_SERVER_TESTS} -DSTATIC_CLIENT=${DSTATIC_CLIENT}"
cmake3 .. -DBUILD_CLIENT=${DBUILD_CLIENT} -DBUILD_SERVER=${DBUILD_SERVER} -DBUILD_SERVER_TESTS=${DBUILD_SERVER_TESTS} -DSTATIC_CLIENT=${DSTATIC_CLIENT}
make
cp "${SLATE_VOLUME_DIR}/build/slate-service" /usr/bin/slate-service

# Start slate-service:
slate-service \
--allowAdHocApps True \
--bootstrapUserFile "${SLATE_VOLUME_DIR}/resources/docker/users/slate_portal_user" \
--config "${SLATE_VOLUME_DIR}/resources/docker/secrets/slate.conf"