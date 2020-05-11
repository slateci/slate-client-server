# Running the server tests

The server code comes with a test suite of what are not really ideal unit tests, but should provide fairly good coverage of the API methods. Unfortunately, the tests are slightly picky about the environment they need in order to run, due to the many moving pieces involved in having the server do anything useful. The environment needs to have: 

- A local install of DynamoDB, which in turn requires java
- The $`DYNAMODB_JAR` environment variable set to the path of DynamoDBLocal.jar
- The $`DYNAMODB_LIB` environment variable set to the path of DynamoDBLocal_lib directory
- minikube, in order to run a minimal kubernetes cluster

With these components, `make check` or `ctest` run in the build directory should be able to run the tests. The `-j` flag to `ctest` can be used to run several tests in parallel, but it should be noted that the maximum number of tests to be run should usually be _half_ the number of logical cores available on the system due to the high overhead of running a DynamoDB/java instance for each test. 

The tests will use whatever Kubernetes environment is currently available, so be careful that your config/context is set appropriately before running the tests. A typically configured minikube instance (2 virtual cores, 2 GB of RAM) may experience difficulties running more than two tests concurrently. 

In some cases it may be desirable to run a test directly without `ctest` as an intermediary. To do this one must manually run the 'test/init_test_env.sh' script, which starts minikube if necessary, starts the test helm repository, and runs the 'slate-test-database-server' daemon, which coordinates starting DynamoDB instances and assigning ports for the various servers run during testing. When testing is complete the 'test/clean_test_env.sh' script should be run to stop minikube if it was started by init_test_env.sh and to stop the database-server. 
