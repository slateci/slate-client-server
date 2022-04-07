#!/bin/bash

# Enable strict mode:
set -euo pipefail

. /slate/resources/docker/scripts/build.sh

echo "Starting slate-service..."
/slate/build/slate-service \
--config "/slate/resources/docker/conf/slate.conf"
