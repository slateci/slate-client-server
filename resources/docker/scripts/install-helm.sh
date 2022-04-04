#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Script variables:
export HELM_VERSION="${1}"

cd "/tmp"
echo "Downloading Helm3 version: ${HELM_VERSION}..."
curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz
curl -LO https://get.helm.sh/helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum

echo "Verifying download..."
sha256sum -c helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum || exit 1

echo "Installing Helm3..."
tar xzf helm-v${HELM_VERSION}-linux-amd64.tar.gz
mv linux-amd64/helm /usr/local/bin/helm

echo "Cleaning up..."
rm -rf helm-v${HELM_VERSION}-linux-amd64.tar.gz helm-v${HELM_VERSION}-linux-amd64.tar.gz.sha256sum linux-amd64
