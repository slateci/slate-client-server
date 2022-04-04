#!/bin/bash

# Enable strict mode:
set -euo pipefail

cd "/tmp"
echo "Downloading latest AWS CLI..."
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o awscliv2.zip
unzip awscliv2.zip

echo "Installing AWS CLI..."
./aws/install

echo "Cleaning up..."
rm awscliv2.zip