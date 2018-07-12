#!/bin/sh
echo "Starting minikube"
minikube -p 'slate_server_test_kube' start --memory 1024 --disk-size 2g --cpus=1
echo "Starting Dynamo server"
./slate-test-database-server &
sleep 1 # wait for server to start
echo "Initialization done"