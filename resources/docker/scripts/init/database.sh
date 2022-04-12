#!/bin/bash

# Enable strict mode:
set -euo pipefail

cd /home/dynamodblocal
java -jar DynamoDBLocal.jar -sharedDb -dbPath ./data