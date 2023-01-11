#!/bin/bash

# Enable strict mode:
set -euo pipefail

ansible-playbook -v -i ./playbook/inventory/hosts.yml ./playbook/playbook.yml
