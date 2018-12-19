#!/bin/sh
# Make sure we are in the right place before doing a mass delete
kubectl config use-context 'slate-server-test-kube'
if [ "$?" -eq 0 ]; then # only if setting context succeeded
	echo "Deleting all 'clusters'"
	CLUSTERS=$(kubectl get clusters -o jsonpath='{.items[*].metadata.name}')
	if [ "$CLUSTERS" ]; then
		kubectl delete clusters $CLUSTERS > /dev/null
	fi
	unset CLUSTERS
fi
if [ -f .stop_minikube_after_tests ]; then
	echo "Stopping minikube"
	minikube -p 'slate-server-test-kube' stop
	rm .stop_minikube_after_tests
fi
echo "Stopping Dynamo server"
curl -s -X PUT 'http://localhost:52000/stop'
