#!/bin/bash

# Enable strict mode:
set -euo pipefail

. /slate/resources/docker/scripts/build.sh

echo "Starting slate-service..."
ln -sf /slate/build/slate-service /usr/bin/slate-service
slate-service \
--config "/slate/resources/docker/conf/slate.conf"
