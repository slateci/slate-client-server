#!/bin/bash

# Enable strict mode:
set -euo pipefail

# Create the bash history file if necessary:
if [ ! -f "$HISTFILE" ]
then
  touch /slate/.bash_history_docker
fi

#echo "Setting up local Rancher K3s..."
#kubectl config set clusters.default.server https://slate_rancher:6443

#echo "Seeding sample data..."
#slate cluster create my-cluster --group my-group --org SLATE --no-ingress -y --kubeconfig /etc/rancher/k3s/k3s.yaml
#echo -e "\e[1m=============================================================\e[0m"
#echo -e "\e[1mDefault Group: my-group\nDefault Cluster: my-cluster\e[0m"

/bin/bash