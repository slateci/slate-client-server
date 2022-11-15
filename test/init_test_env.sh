#!/bin/sh

TEST_SOURCE_DIR=`dirname $0`
INITIAL_DIR=`pwd`

wait_pod_ready(){
	PODNAME=$1;
	echo "Waiting for $PODNAME to be ready"
	READY="0 -eq 1"
	unset DELAY # don't sleep on first iteration
	until [ "$READY" ]; do
		if [ "$DELAY" ]; then
			sleep $DELAY
		else
			DELAY=1
		fi
		STATUS=`kubectl get pods --namespace kube-system 2>/dev/null | grep "$PODNAME"`
		COUNT=`echo "$STATUS" | awk '{print $2}'`
		echo "    Containers: $COUNT ("`echo "$STATUS" | awk '{print $3}'`")"
		READY=`echo "$COUNT" | sed 's|\(.*\)/\(.*\)|\1 -eq \2|'`' -a '`echo "$STATUS" | awk '{print $3}'`' = Running'
	done
}

# MINIKUBE_STATUS=`minikube -p 'slate-server-test-kube' status`
# if [ `echo "$MINIKUBE_STATUS" | grep -c Running` -ge 2 \
#     -a `echo "$MINIKUBE_STATUS" | grep -c 'Correctly Configured'` -eq 1 ] ; then
# 	echo "Using running minikube instance"
# else
# 	echo "Starting minikube"
# 	touch .stop_minikube_after_tests
# 	minikube -p 'slate-server-test-kube' start
# fi
# wait_pod_ready "kube-apiserver-minikube"
# # these components get nasty auto-generated names, but have predictable prefixes
# wait_pod_ready "kube-proxy"
# wait_pod_ready "dns"

echo "Starting Dynamo server"
./slate-test-database-server &
DBSERVER="$!"
until [ -f .test_server_ready ]; do
	sleep 1 # wait for server to start
done
if ps -p "${DBSERVER}" > /dev/null ; then
: # good
else
	echo "DBServer failed" 1>&2
	exit 1
fi
echo "Preparing local helm repository"
mkdir -p test_helm_repo
cp -a "$TEST_SRC"/test_helm_repo .
helm package "$TEST_SOURCE_DIR"/test_helm_repo/test-app -d test_helm_repo > /dev/null
helm repo index test_helm_repo
# request running the helm server
echo "Starting local helm server"
curl -s 'http://localhost:52000/helm'
helm repo update > /dev/null
helm repo add local http://localhost:8879
echo "Initialization done"
