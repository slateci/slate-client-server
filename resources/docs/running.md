# Running the Server

## Required Inputs

In order to start, `slate-service` requires the following

- A file called 'slate_portal_user' in its CWD. This file should be ASCII text containing on separate lines:
	- A user ID for the web portal user, which should have the form User_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}
	- The web portal user's name, canonically 'WebPortal'
	- A contact email address for the SLATE platform
	- The web portal user's private access token. This typically has the form of a version 4 (random UUID), although this is not currently required. 
	
- A file called 'encryptionKey' which contains 1024 bytes of data used as the encryption key for sensitive data in the persistent store. If this key is lost such data cannot generally be recovered. 

- Either the $`HELM_HOME` or $`HOME` environment variable must be set; the first must refer to the '.helm' directory in which `helm`'s data is stored, while the latter must refer to the containing directory. (In the case that $`HELM_HOME` is used the directory need not actually be named '.helm'.)

- A DynamoDB instance: By default the service will attempt to contact one at http://localhost:8000 . See the next section for options to change this. 

## Optional settings

A number of settings for `slate-service` can be changed on startup. Each option generally has both an environment variable and a command line flag. If both are present, the value passed to the flag will take precedence. 

- `--awsAccessKey` [$`SLATE_awsAccessKey`] specifies the access key used when contacting DynamoDB (default: 'foo')
- `--awsSecretKey` [$`SLATE_awsSecretKey`] specifies the access key used when contacting DynamoDB (default: 'bar')
- `--awsRegion` [$`SLATE_awsRegion`] specifies the AWS region used when contacting DynamoDB (default: 'us-east-1')
- `--awsURLScheme` [$`SLATE_awsURLScheme`] specifies the scheme used when contacting DynamoDB valid values are 'http' and 'https' (default: 'http')
- `--awsEndpoint` [$`SLATE_awsEndpoint`] specifies the hostname/IP address and port used when contacting DynamoDB (default: 'localhost:8000')
- `--port` [$`SLATE_PORT`] specifies the port on which `slate-service` will listen (default: 18080)
- `--ssl-certificate` [$`SLATE_SSL_CERTIFICATE`] specifies the SSL certificate to be used when serving requests. If specified `--ssl-key` must also be used or $`SLATE_SSL_KEY` set. Use of these options implicitly makes all connections to `slate-service` require the `https` scheme. - `--ssl-key` [$`SLATE_SSL_KEY`] specifies the SSL certificate key to be used when serving requests. If specified `--ssl-certificate` must also be used or $`SLATE_SSL_CERTIFICATE` set. Use of these options implicitly makes all connections to `slate-service` require the `https` scheme. 

If an SSL certificate is set, the files referred to by `--ssl-certificate`/$`SLATE_SSL_CERTIFICATE` and `--ssl-key`/$`SLATE_SSL_KEY` must be readable by `slate-service`. 

## Running a local DynamoDB instance

For testing it is useful to run an instance of DynamoDB locally. See [the AWS documentation](https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/DynamoDBLocal.html) for details on obtaining the local version of Dynamo. Note that a reasonably new version of the JRE is required. The basic command to start Dynamo is

	java -Djava.library.path=./DynamoDBLocal_lib -jar DynamoDBLocal.jar
	
assuming it is being run from the directory in which the components have been unpacked. It may be useful to the database as a background process during testing. 
