#!/bin/sh
echo "Stopping minikube"
minikube -p 'slate_server_test_kube' stop
echo "Stopping Dynamo server"
curl -s -X PUT 'http://localhost:52000/stop'