#!/bin/sh

TEST_SOURCE_DIR=`dirname $0`
INITIAL_DIR=`pwd`

wait_pod_ready(){
	PODNAME=$1;
	echo "Waiting for $PODNAME to be ready"
	REPLICAS="0 -eq 1"
	until [ $REPLICAS ]; do
	sleep 1
		STATUS=`kubectl get pods --namespace kube-system 2>/dev/null | grep "$PODNAME"`
		REPLICAS=`echo "$STATUS" | awk '{print $2}'`
		echo "    Replicas: $REPLICAS ("`echo "$STATUS" | awk '{print $3}'`")"
		REPLICAS=`echo "$REPLICAS" | sed 's|\(.*\)/\(.*\)|\1 -eq \2|'`
	done
}

echo "Starting minikube"
minikube -p 'slate_server_test_kube' start
wait_pod_ready "kube-apiserver-minikube"
# these components get nasty auto-generated names, but have predictable prefixes
wait_pod_ready "kube-proxy"
wait_pod_ready "kube-dns"
echo "Starting Dynamo server"
./slate-test-database-server &
sleep 1 # wait for server to start
echo "Preparing local helm repository"
mkdir -p test_helm_repo
helm package "$TEST_SOURCE_DIR"/test_helm_repo/test-app > /dev/null
rm -f test-app-*.tgz
# request running the helm server
curl -s 'http://localhost:52000/helm'
helm repo update > /dev/null
echo "Initialization done"