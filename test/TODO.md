# Issue #140: Skip Cascading Deletion

Adds an option to force delete an unreachable cluster. This will skip the standard method of cascading deletion in which objects on the backend are deleted in succession. Since the cluster is unreachable, now a message will appear stating that the cluster cannot be contacted. Once a `--force` flag is provided a different message will prompt the user if they want to skip object deletion and simply remove database entries. If a `-y` flag is provided or the user answers `yes`, then the slate-client-server will delete the database entries and move on.

A new test was written to ensure that this feature continues to work, but some issues were noted with the new test in a pull-request to close issue #140. In order to make a cluster unreachable, the `clusterCacheValidity` in `persistentStore.cpp` time must be set to a shorter period than the default 30 minutes. This time is added to a `std::chrono::steady_clock::now()`  method and this defines the `clusterCacheExpirationTime` which will force slate to check the database when searching for a clusters stored kubeconfig.

The test written is designed to update the cluster with an invalid kubeconfig, set clusterCacheValidity to 1 second, and then run a `slate cluster delete --force [CLUSTER_ID]` to ensure that cascading deletion is skipped and the cluster records are then deleted. Following this, a database entry check is performed to ensure that the entries were successfully deleted.

Unfortunately, to set the clusterCacheValidity time to 1 second using an `add cluster` command with a new option defined in `clusterCommands.cpp` called `setCacheValidity` is a bad idea, and users should not be able to arbitrarily change the global cache settings. Chris Weaver left some comments on this issue and how to change the clusterCacheValidity time using the CLI configuration code. Below are some notes he left in a previous pull-request.

## Comments

Allowing any user to change the server's caching period when  adding a cluster is very bad and must be removed. The correct way to to  approach this is to 1) update [the CLI configurtion code](https://github.com/slateci/slate-client-server/blob/skipCascadingDeletion/src/slate_service.cpp#L110) to support setting the cache validity period(s) with options, 2) set those on the `PersistentStore` after it is constructed, and 3) change the test to set the new option(s) on the server that it starts via the `TestContext`, using the support it [already has for setting options](https://github.com/slateci/slate-client-server/blob/skipCascadingDeletion/test/test.h#L147).

The breakage of all of the other API object's user-facing deletion functions is also a blocking issue and must be resolved.

```c++
// COMMENT 1
include/PersistentStore.h
@@ -282,7 +288,7 @@ class PersistentStore{
282	///Store a record for a new cluster
283	///\param cluster the new cluster
284	///\return Whether the record was successfully added to the database
285 -	bool addCluster(const Cluster& cluster);
291	+   bool addCluster(const Cluster& cluster, int cacheExpirationTime = 0);
Member
@cnweaver cnweaver 4 days ago

//I don't think that setting the cache validity time should be a part of this function's interface. If you want to change the validity period used when considering cluster records going forward, setClusterCacheValidity should just be called before calling addCluster.

```

```c++
// COMMENT 2
src/ClusterCommands.cpp
411 -		log_error("Failed to create " << cluster);
412 -		return crow::response(500,generateError("Cluster registration failed"));
409 +
410 +	if(body["metadata"].HasMember("setCacheValidity")){ //optional configuration for cluster cache validity time
Member
@cnweaver cnweaver 4 days ago

//This emphatically should not be exposed this way in the API. Users should not be able to arbitrarily change the global cache settings.
```

```c++
// COMMENT 3
src/PersistentStore.cpp
@@ -1928,7 +1933,7 @@ SharedFileHandle PersistentStore::configPathForCluster(const std::string& cID){
	return clusterConfigs.find(cID);
}

1931 - bool PersistentStore::addCluster(const Cluster& cluster){
1936 + bool PersistentStore::addCluster(const Cluster& cluster, int cacheExpirationTime){
Member
@cnweaver cnweaver 4 days ago

//As noted above, this feature should not be implemented here.
```

```c++
// COMMENT 4
test/TestClusterDeletion.cpp
340 +		metadata.AddMember("group", rapidjson::StringRef(groupID), alloc);
341 +		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
342 +		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
343 +		metadata.AddMember("setClusterCacheValidity", 1, alloc);
Member
@cnweaver cnweaver 4 days ago

//This is not the right way to handle setting this.
```

### Note

While other concerns were easily fixed, the comments above allude to a security concern in which a user can arbitrarily set the `clusterCacheValidityTime` and, as a result, the `clusterCacheExpirationTime` as well.

This means a user could potentially overload the database by setting the `clusterCacheValidity` time to a very short interval on the SLATE production API server. Since caching is used for accessing most of our data, it would be very expensive if the database was being queried for every data request. While the option is hidden to most users, a saboteur could find this option in the API server and run an HTTP request to add a new cluster and alter the `clusterCacheValidity`.

To get around this issue, Chris recommended that the option be set in the CLI configuration code in the `slate_service.cpp` file. Then the option could be called in the `TestContext` options.

Some issues were initially encountered when attempting this before. First is that in the `slate_service.cpp` file it seems that you cannot set a configuration option for the data type of `std::chrono::[time_interval]` such as `std::chrono::seconds`, and thus you can't directly set the configuration option to the correct data type for `clusterCacheValidity` time.

Next it was noted that the `TestContext` will need public access to the new configuration option in order to change the persistent store configuration on startup.

In addition to this, somehow this new value for `clusterCacheValidity` needs to overwrite the default time of 30 minutes (1800 seconds). Potentially this could be done using the already existing method of `setClusterCacheValidity()` in `persistentStore.cpp`. Strangely enough, however, this method only seemed to work properly when it was run as part of the `addCluster()` in the persistent store.

If `setClusterCacheValidity()` was called by itself <u>in the test</u> then a get method `getClusterCacheValidity()` would return the new cache validity of 1 second, as expected. But when that same get method was called as part of a method in the `persistetntStore.cpp`, it would return the default 1800 seconds. 

This was a source of much confusion; it could be it has something to do with the `TestContext`, or a problem with calling persistent store methods directly in a test. HTTP requests seem to work just fine, but that is where we ran into our security issue as described above.

### Suggestion

First create a new configuration option in `slate_service.cpp` to set a desired integer in seconds.

 Then find a way to pass that integer to the persistent store configuration and replace the default value at `persistentstore.cpp Line #231` from `std::chrono::minutes(30)` to `std::chrono::seconds(foo)` after the persistent store is constructed. 

Then ensure that the `TestContext` has access to that configuration option and set it to a short time interval like 1 second. 

If everything is successful, when running `ctest -V -R test-cluster-deletion`  the test called `ForceDeletingUnreachableCluster` should pass and report that the cluster components cannot be reached.

**Cleanup**

After all that, be sure to clean up old changes made to `crow::response createCluster()` method in `ClusterCommands.cpp`, changes made to the `addCluster()` method in `PersistentStore.cpp`, and the option added to the `create cluster HTTP request` in the `ForceDeletingUnreachableCluster` test.

