#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Script variables:
CONTAINER_RUNTIME="containerd"
KUBERNETES_VERSION="1.22.15"
MINIKUBE_ADDONS=["metallb"]
MINIKUBE_CNI="auto"
MINIKUBE_DRIVER="${1:-virtualbox}"
MINIKUBE_PROFILE="slate-local"

# Start Minikube:
minikube start \
  --addons=$MINIKUBE_ADDONS \
  --cni="${MINIKUBE_CNI}" \
  --container-runtime="${CONTAINER_RUNTIME}" \
  --driver="${MINIKUBE_DRIVER}" \
  --kubernetes-version="${KUBERNETES_VERSION}" \
  --profile="${MINIKUBE_PROFILE}"

if [[ "$(kubectl config current-context)" == "${MINIKUBE_PROFILE}" ]]
then
  helm secrets diff slate-api-local . \
    -f ./vars/local/secrets.yml \
    -f ./vars/local/values.yml \
    -f ./vars/secrets.yml \
    -f ./vars/values.yml \
    -n default \
    --allow-unreleased \
    --show-secrets
else
  echo "kubectl is not set to the context: ${MINIKUBE_PROFILE}"
fi