# Running the server tests

## Overview

The server code comes with a test suite of what are not really ideal unit tests, but should provide fairly good coverage of the API methods. Unfortunately, the tests are slightly picky about the environment they need in order to run, due to the many moving pieces involved in having the server do anything useful. The environment needs to have:

- A local installation of DynamoDB, which in turn requires java
- The $`DYNAMODB_JAR` environment variable set to the path of DynamoDBLocal.jar
- The $`DYNAMODB_LIB` environment variable set to the path of DynamoDBLocal_lib directory
- minikube, in order to run a minimal kubernetes cluster

With these components, `make check` or `ctest` run in the build directory should be able to run the tests. The `-j` flag to `ctest` can be used to run several tests in parallel, but it should be noted that the maximum number of tests to be run should usually be _half_ the number of logical cores available on the system due to the high overhead of running a DynamoDB/java instance for each test.

The tests will use whatever Kubernetes environment is currently available, so be careful that your config/context is set appropriately before running the tests. A typically configured minikube instance (2 virtual cores, 2 GB of RAM) may experience difficulties running more than two tests concurrently.

In some cases it may be desirable to run a test directly without `ctest` as an intermediary. To do this one must manually run the `test/init_test_env.sh` script, which starts minikube if necessary, starts the test helm repository, and runs the `slate-test-database-server` daemon, which coordinates starting DynamoDB instances and assigning ports for the various servers run during testing. When testing is complete the `test/clean_test_env.sh` script should be run to stop minikube if it was started by init_test_env.sh and to stop the database-server.

## Local development

In order to facilitate ease of local development the team has created several containers and [JetBrains CLion](https://www.jetbrains.com/clion/) tools for use in locally developing SLATE. Feel free to do your own thing instead but note that support from the group may be somewhat limited.

### Requirements

1. A x86-based machine. If your local machine does not meet this requirement (e.g. an M1 Mac), refer to [slateci/cloudlabprofile-localdev](https://github.com/slateci/cloudlabprofile-localdev) to access an x86-based remote machine as a CloudLab experiment.
2. A local installation of either:
    * **Podman -** `podman` and `podman-compose` or
    * **Docker -** `docker` and `docker-compose` (pre-installed in the CloudLab experiment)

### (Optional) Configure/connect CLion to remote

If you are using a machine on CloudLab configure CLion for remote development on that machine by choosing **[ File] --> [ Remote Development ]**.

1. Create a new connection.
2. Click the gear icon and fill out the connection details. For example:

   ![clion remote development](./images/clion_cloudlab_sshconfig.png)
3. Finish the connection wizard in CLion and the IDE should open.

### Running Tests

1. On a machine start up the containerized local development environment at the root of this repository.
   ```shell
   $ cd ./clion
   $ ./start_minikube.sh
   $ cd ..
   $ podman-compose up --file podman-compose.yml
   ```
   or if you are using Docker:
   ```shell
   $ docker-compose up
   ```
   At this point Kubernetes and the local development container (`clionremote`) should be active.

2. Open CLion and prepare the toolchain settings for `clionremote`. The credentials are:
   
   | Field | Value |
   |-------| ----  |
   | username | `root` |
   | password | `password` |
   | port | `2222` |

   If configured successfully you should see a dialog with green checkmarks like:

   ![clion toolchain settings](./images/clion_settings_toolchain.png)

3. Then prepare the CMake settings for use with the newly created toolchain.

   | Field      | Value |
   | --- | --- |
   | Build type | `Debug` |
   | Toolchain  | `clionremote` |

   ![clion cmake settings](./images/clion_settings_cmake.png)

4. Finally, in CLion configure the deployment path for `clionremote`:
   
   | Field | Value |
   | --- | --- |
   | Deployment path     | `/tmp/work` |

   ![clion deployment settings](./images/clion_settings_deployment.png)

5. Close the **Settings** dialog and build the project by choosing **[ Build ] --> [ Build the Project ]** in the primary CLion toolbar.

6. Open the **Run/Debug configurations** dialog and create a new CTest application **All Tests** entry.

   ![clion run/debug all tests configuration](./images/clion_buildrun_configurations_alltests.png)

7. Run **All Tests**.
   * If you receive a "permission denied" the first go-around, click the green Run icon in the **Run** panel again.

   The following output will be displayed in the **Run** panel (expand from the bottom of the CLion window).

   ![clion run/debug all tests](./images/clion_buildrun_alltests.png)

8. Alternatively run pre-defined groups of tests (stored in this repo at `./clion/runConfigurations`).

   ![clion run/debug group tests configuration](./images/clion_buildrun_configurations_group.png)