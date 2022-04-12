#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Running unit tests..."
cd "/slate/build"
ctest3
