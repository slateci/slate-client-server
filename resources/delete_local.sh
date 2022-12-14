#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Script variables:
MINIKUBE_PROFILE="slate-$(whoami)"

# Delete Minikube profile:
minikube delete --profile="${MINIKUBE_PROFILE}" --all