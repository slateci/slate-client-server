#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Starting slate-service..."
/slate/build/slate-service \
--allowAdHocApps True \
--config "/slate/resources/docker/conf/slate.conf"
