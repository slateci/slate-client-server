#!/bin/bash

# Enable strict mode:
set -euo pipefail

. /slate/resources/docker/scripts/build.sh

echo "Starting slate-service..."
ln -s /slate/build/slate-service /usr/local/bin/slate-service
slate-service \
--allowAdHocApps True \
--config "/slate/resources/docker/conf/slate.conf"
