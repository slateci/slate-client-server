# RPM Building instructions for slate server rpms

## Using cmake

This is probably the easiest way to build the rpms.  Configure the cmake build
and then run `make server-rpm`.  

## Manually

This method is a bit more involved.  In a centos 7 container:

* Download the aws c++ sdk (e.g.`aws-sdk-cpp-1.7.345.tar.gz`) from the github repo.  
* Place the tarball in the `$RPM_BUILD_ROOT/SOURCES` directory.  
* Put the `aws-sdk-cpp.spec` file from the `rpm_specs` dir in the`$RPM_BUILD_ROOT/SPECS` directory 
* Edit the `aws-sdk-cpp.spec` and set the `Version` tag to the version of the aws sdk (e.g. `1.7.345`)
* Run `rpmbuild -bb aws-sdk-cpp.spec` in the `$RPM_BUILD_ROOT/SPECS` directory
* Install the generated binary rpms from `$RPM_BUILD_ROOT/RPMS` directory
* Place the `slate-api-server.spec` from the `rpm_specs` dir in the`$RPM_BUILD_ROOT/SPECS` directory
* Place a tarball of this repo in the `$RPM_BUILD_ROOT/SOURCES`directory 
* Run `rpmbuild -bb slate-api-server.spec` in the `$RPM_BUILD_ROOT/SPECS`directory
* The generated binaries should be in the `$RPM_BUILD_ROOT/RPMS/x86_64`

