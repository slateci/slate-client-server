#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Start slate-service
slate-service --conf /slate/conf/slate.conf
