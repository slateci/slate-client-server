# Instructions on running unit tests

## Caveats
These instructions are based on running things using podman.  Using docker may work 
as well but functionality is not guaranteed.

## Dev/Compilation Container setup
Build the dev/compilation container using `podman build -f clion_remote.Dockerfile -t [tag]`.
After that's complete, you can run the container 
using  `podman run -d --cap-add sys_ptrace -p127.0.0.1:2222:22 -p127.0.0.1:18080:18080 localhost/[tag]`.

Check that you can access the container by running `ssh clionremote@localhost -p 2222`, by default
the password is `password`.  This should be safe since the container is only accessible from the local
machine.

### Setup for profiling
In order to run `perf` in the container, you'll need run the container with elevated privileges. 
First on the host machine, you'll need to run `sysctl -w kernel.perf_event_paranoid=1` as **root**. 
(Remember to set this back when you're done).  Then run 
`podman run -d --privileged --cap-add sys_ptrace --cap-add SYS_ADMIN -p127.0.0.1:2222:22 -p127.0.0.1:18080:18080  --security-opt seccomp=perf.json`.
Note the `perf.json` file is in the same directory as the `clion_remote.Dockerfile`.  This should allow 
you to run perf and get profiling on slate components within the container.

## Minikube setup
Outside the container, create a k8s cluster locally by running `minikube start`.  Then
copy the credentials to the container using
`scp -P 2222 ~/.minikube/ca.crt ~/.minikube/profiles/minikube/client.crt ~/.minikube/profiles/minikube/client.key  clionremote@localhost:~/.kube/`.
Create a config file at `~/.kube/config` with the following:

```yaml
apiVersion: v1
clusters:
- cluster:
    certificate-authority: ca.crt
    extensions:
    - extension:
        last-update: Wed, 08 Sep 2021 10:34:26 HST
        provider: minikube.sigs.k8s.io
        version: v1.22.0
      name: cluster_info
    server: https://[ip]:8443
  name: minikube
contexts:
- context:
    cluster: minikube
    user: minikube
  name: minikube
current-context: minikube
kind: Config
preferences: {}
users:
- name: minikube
  user:
    client-certificate: client.crt
    client-key: client.key


``` 

Make sure to replace `[ip]` with the ip address given when `minikube ip` is run.

Verify everything is working by running `kubectl get ns`, you should see something like the following:

```commandline
NAME              STATUS   AGE
default           Active   3h47m
kube-node-lease   Active   3h47m
kube-public       Active   3h47m
kube-system       Active   3h47m
```

## Local dynamodb setup

---
**_NOTE:_** In this section `[src_dir]` refers to the directory with the source code, it's
usually something like `/tmp/tmp.[char_string]` where `[char_string]` is an alphanumeric string.

**_NOTE:_** Additionally `[profile]` is the CLion profile that you're using.  E.g. `remote-debug`.

---


If there is no `/tmp/db.tar.gz` file, download the dynamodb files using 
`curl  https://s3.us-west-2.amazonaws.com/dynamodb-local/dynamodb_local_latest.tar.gz -o  db.tar.gz`.  
Then run `tar xvzf db.tar.gz`.  

Additionally, go to `/tmp[src_dir]/cmake-build-[profile]` where  and  untar the `db.tar.gz` file there as well.


## Setup test environment

---
**_NOTE:_** In this section `[src_dir]` refers to the directory with the source code, it's
usually something like `/tmp/tmp.[char_string]` where `[char_string]` is an alphanumeric string.

**_NOTE:_** Additionally `[profile]` is the CLion profile that you're using.  E.g. `remote-debug`.

---

First build all the test binaries for the project and then in the `[src_dir]/cmake-build-debug` directory, run
the following: 
* `cp slate-test-database-server /tmp/`
* `cp slate-test-helm-server /tmp/`
* `cd tests; ln -s ../slate-service slate-service`

In `[src_dir]/test` directory, run the following:
* `export TEST_SRC=/tmp/[src_dir]/test`
* `cp init_test_env.sh  /tmp/`

Finally, run `init_test_env.sh`

### Running tests

### Running unit tests under CLion
CLion should automatically pick up the tests using `CTest` and there should be a CTest profile setup. The 
`Select Run/Debug Configuration` dropdown on the upper right side of the window should have a `All CTest` option to
run all the tests.  

### Manually running the unit tests
---
**_NOTE:_** In this section `[src_dir]` refers to the directory with the source code, it's
usually something like `/tmp/tmp.[char_string]` where `[char_string]` is an alphanumeric string.
---

* Run `export SLATE_SCHEMA_DIR=/tmp/[src_dir]/resources/api_specification`.
* Go to `[src_dir]/cmake-build-debug/tests`
* Run any of the `test-*` binaries to run a given test
