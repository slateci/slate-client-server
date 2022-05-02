#!/bin/bash

set -euo pipefail

# Fetch all nodes and their names only
NODES=$(kubectl get nodes | tail -n +2 | cut -d' ' -f1)

# Read each node name
while IFS= read -r line; do
	echo "Node: $line"

	# Call kubectl describe on each node and grep for CPU and Memory, last 2 lines only
	RESOURCE_STATS=$(kubectl describe node "$line" | grep 'cpu \| memory' | tail -n2)
	
	# Read each line from RESOUCE_STATS
	while read line; do
		# Get CPU and Memory  requests inside the parentheses
		echo "$line" | cut -d'(' -f2 | cut -d')' -f1
	done <<< "$RESOURCE_STATS"
done <<< "$NODES"
