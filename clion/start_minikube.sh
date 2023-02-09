#!/bin/bash

# NOTES:
#
# For those on Rocky9,
# * If using Virtual Box, then >= 7.0.6 is required.
#
# Under development on Rocky 9,
# * If using Podman (and docker?) with cgroupV2 require extra steps as described in https://bugzilla.redhat.com/show_bug.cgi?id=1897579.
#   * Create /etc/systemd/system/user@.service.d/delegate.conf
#   * Create /etc/systemd/system/user-.slice.d/override.conf
#   * Then execute `sudo systemctl daemon-reload`


# Enable strict mode:
set -euo pipefail

# Script variables:
CONTAINER_RUNTIME="containerd"
KUBERNETES_VERSION="1.24.9"
MINIKUBE_CNI="calico"
MINIKUBE_CPUS=2
MINIKUBE_DRIVER="${1:-virtualbox}" # typical values are docker, podman, virtualbox
MINIKUBE_MEMORY=4092 # in Megabytes
MINIKUBE_NODES=2
MINIKUBE_PROFILE="slate-$(whoami)"

# Start Minikube:
if [[ "${MINIKUBE_DRIVER}" == "docker" ]] || [[ "${MINIKUBE_DRIVER}" == "podman" ]]
then
  echo "IMPORTANT: This option is still under development -- trying to get kubectl in clionremote working correctly"
#  minikube start \
#    --addons="metrics-server" \
#    --cni="${MINIKUBE_CNI}" \
#    --container-runtime="${CONTAINER_RUNTIME}" \
#    --cpus=$MINIKUBE_CPUS \
#    --driver="${MINIKUBE_DRIVER}" \
#    --kubernetes-version="${KUBERNETES_VERSION}" \
#    --listen-address='0.0.0.0' \
#    --memory=$MINIKUBE_MEMORY \
#    --network-plugin=cni \
#    --nodes $MINIKUBE_NODES \
#    --profile="${MINIKUBE_PROFILE}" \
#    --rootless
else
  minikube start \
    --addons="metrics-server" \
    --cni="${MINIKUBE_CNI}" \
    --container-runtime="${CONTAINER_RUNTIME}" \
    --cpus=$MINIKUBE_CPUS \
    --driver="${MINIKUBE_DRIVER}" \
    --kubernetes-version="${KUBERNETES_VERSION}" \
    --memory=$MINIKUBE_MEMORY \
    --network-plugin=cni \
    --nodes $MINIKUBE_NODES \
    --profile="${MINIKUBE_PROFILE}"
fi

# Export the kubeconfig
echo "Exporting the kubeconfig:"
kubectl config view --minify --flatten --context "${MINIKUBE_PROFILE}" | tee ./kubernetes/kubeconfig.yaml

# Create local namespace
echo "Creating 'local' namespace:"
kubectl create namespace local
