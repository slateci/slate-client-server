# Running the server tests

The server code comes with a test suite of what are not really ideal unit tests, but should provide fairly good coverage of the API methods. Unfortunately, the tests are slightly picky about the environment they need in order to run, due to the many moving pieces involved in having the server do anything useful. The environment needs to have: 

- A local install of DynamoDB, which in turn requires java
- The $`DYNAMODB_JAR` environment variable set to the path of DynamoDBLocal.jar
- The $`DYNAMODB_LIB` environment variable set to the path of DynamoDBLocal_lib directory
- minikube, in order to run a minimal kubernetes cluster

With these components, `make check` or `ctest` run in the build directory should be able to run the tests. The `-j` flag to `ctest` can be used to run several tests in parallel, but it should be noted that the maximum number of tests to be run should usually be _half_ the number of logical cores available on the system due to the high overhead of running a DynamoDB/java instance for each test. 

It is also worth noting that the time to fully start up minikube is considerable (multiple minutes), so it may be desirable to leave the minikube instance running if repeated tests are required (such as during debugging). The test environment uses a minikube 'profile' named 'slate_server_test_kube', and if such an instance is already running when the tests are started it will not be stopped. One may therefore run

	minikube -p 'slate_server_test_kube' start

prior to the tests to shorten iteration time. 

In some cases it may be desirable to run a test directly without `ctest` as an intermediary. To do this one must manually run the 'test/init_test_env.sh' script, which starts minikube if necessary, starts the test helm repository, and runs the 'slate-test-database-server' daemon, which coordinates starting DynamoDB instances and assigning ports for the various servers run during testing. When testing is complete the 'test/clean_test_env.sh' script should be run to stop minikube if it was started by init_test_env.sh and to stop the database-server. 