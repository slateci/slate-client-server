#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Script variables:
MINIKUBE_PROFILE="slate-$(whoami)"

# Delete Minikube profile:
echo "Deleting Minikube profile: ${MINIKUBE_PROFILE}"
minikube delete --profile="${MINIKUBE_PROFILE}" --all

# Remove the kubeconfig:
if [[ -f ./kubernetes/kubeconfig.yaml ]]
then
  echo "Cleaning up associated kubeconfig.yaml"
  rm ./kubernetes/kubeconfig.yaml
fi
