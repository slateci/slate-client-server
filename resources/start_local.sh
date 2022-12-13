#!/bin/bash

# NOTES:
#
# For those on Rocky9,
# * VirtualBox doesn't currently work, see https://forums.rockylinux.org/t/the-vboxdrv-kernel-module-is-not-loaded-error-after-upgrading-kernel/8036/3.
# * Podman (and docker?) with cgroupV2 require extra steps as described in https://bugzilla.redhat.com/show_bug.cgi?id=1897579.
#   * Create /etc/systemd/system/user@.service.d/delegate.conf
#   * Create /etc/systemd/system/user-.slice.d/override.conf
#   * Then execute `sudo systemctl daemon-reload`


# Enable strict mode:
set -euo pipefail

# Script variables:
CONTAINER_RUNTIME="containerd"
KUBERNETES_VERSION="1.22.15"
MINIKUBE_ADDONS=["metallb"]
MINIKUBE_CNI="auto"
MINIKUBE_DRIVER="${1:-podman}" # typical values are docker, podman, virtualbox
MINIKUBE_NODES=2
MINIKUBE_PROFILE="slate-$(whoami)"

# Start Minikube:
if [[ "${MINIKUBE_DRIVER}" == "docker" ]] || [[ "${MINIKUBE_DRIVER}" == "podman" ]]
then
  minikube start \
    --addons=$MINIKUBE_ADDONS \
    --cni="${MINIKUBE_CNI}" \
    --container-runtime="${CONTAINER_RUNTIME}" \
    --driver="${MINIKUBE_DRIVER}" \
    --kubernetes-version="${KUBERNETES_VERSION}" \
    --nodes $MINIKUBE_NODES \
    --profile="${MINIKUBE_PROFILE}" \
    --rootless
else
  minikube start \
    --addons=$MINIKUBE_ADDONS \
    --cni="${MINIKUBE_CNI}" \
    --container-runtime="${CONTAINER_RUNTIME}" \
    --driver="${MINIKUBE_DRIVER}" \
    --kubernetes-version="${KUBERNETES_VERSION}" \
    --nodes $MINIKUBE_NODES \
    --profile="${MINIKUBE_PROFILE}"
fi

if [[ "$(kubectl config current-context)" == "${MINIKUBE_PROFILE}" ]]
then
  helm secrets install slate-api-local . \
    -f ./vars/local/secrets.yml \
    -f ./vars/local/values.yml \
    -f ./vars/secrets.yml \
    -f ./vars/values.yml \
    -n default
else
  echo "kubectl is not set to the context: ${MINIKUBE_PROFILE}"
fi