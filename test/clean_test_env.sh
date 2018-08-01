#!/bin/sh
if [ -f .stop_minikube_after_tests ]; then
	echo "Stopping minikube"
	minikube -p 'slate_server_test_kube' stop
	rm .stop_minikube_after_tests
fi
echo "Stopping Dynamo server"
curl -s -X PUT 'http://localhost:52000/stop'