#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Start slate-service
slate-service --config /slate/conf/slate.conf
