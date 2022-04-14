#!/bin/bash

# Enable strict mode:
set -euo pipefail

echo "Setting up local Rancher K3s..."
kubectl config set clusters.default.server https://slate_rancher:6443

echo "Running unit tests..."
cd "/slate/build"
ctest3 --verbose
