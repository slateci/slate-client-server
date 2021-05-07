#!/bin/sh
if [ -f .stop_minikube_after_tests ]; then
	echo "Stopping minikube"
	minikube -p 'slate-server-test-kube' stop
	rm .stop_minikube_after_tests
fi
echo "Stopping Dynamo server"
curl -s -X PUT 'http://localhost:52000/stop'
helm repo remove local
echo "Removed local repositiory"
