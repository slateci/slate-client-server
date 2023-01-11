#!/bin/bash

# Enable strict mode:
set -euo pipefail

cd ./playbook
ansible-playbook -v -i ./inventory/hosts.yml ./playbook.yml
