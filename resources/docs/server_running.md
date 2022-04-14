# Running the Server

## Required Inputs

In order to start, `slate-service` requires the following

- A file called 'slate_portal_user' in its CWD. This file should be ASCII text containing on separate lines:
	- A user ID for the web portal user, which should have the form User_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}
	- The web portal user's name, canonically 'WebPortal'
	- A contact email address for the SLATE platform
	- A contact phone number for the SLATE platform
	- The name of an institution with which the platform is associated. 
	- The web portal user's private access token. This typically has the form of a version 4 (random UUID), although this is not currently required. 
	
	If running a local server for testing purposes the email, phone number, and institution need not be meaningful. 
	The name or location of this file may be overridden using the `--bootstrapUserFile` option described below. 
	
- A file called 'encryptionKey' which contains 1024 bytes of data used as the encryption key for sensitive data in the persistent store. If this key is lost such data cannot generally be recovered. The name or location of this file may be overridden using the `--encryptionKeyFile` option described below. 

- Either the $`HELM_HOME` or $`HOME` environment variable must be set; the first must refer to the '.helm' directory in which `helm`'s data is stored, while the latter must refer to the containing directory. (In the case that $`HELM_HOME` is used the directory need not actually be named '.helm'.)

- A DynamoDB instance: By default the service will attempt to contact one at http://localhost:8000 . See the next section for options to change this. 

## Optional settings

A number of settings for `slate-service` can be changed on startup. Each option generally has both an environment variable and a command line option. If both are present, the value passed to the option will take precedence. 

- `--awsAccessKey` [$`SLATE_awsAccessKey`] specifies the access key used when contacting DynamoDB (default: 'foo')
- `--awsSecretKey` [$`SLATE_awsSecretKey`] specifies the access key used when contacting DynamoDB (default: 'bar')
- `--awsRegion` [$`SLATE_awsRegion`] specifies the AWS region used when contacting DynamoDB (default: 'us-east-1')
- `--awsURLScheme` [$`SLATE_awsURLScheme`] specifies the scheme used when contacting DynamoDB valid values are 'http' and 'https' (default: 'http')
- `--awsEndpoint` [$`SLATE_awsEndpoint`] specifies the hostname/IP address and port used when contacting DynamoDB (default: 'localhost:8000')
- `--port` [$`SLATE_PORT`] specifies the port on which `slate-service` will listen (default: 18080)
- `--sslCertificate` [$`SLATE_sslCertificate`] specifies the SSL certificate to be used when serving requests. If specified `--sslKey` must also be used or $`SLATE_sslKey` set. Use of these options implicitly makes all connections to `slate-service` require the `https` scheme. 
- `--ssl-key` [$`SLATE_sslKey`] specifies the SSL certificate key to be used when serving requests. If specified `--sslCertificate` must also be used or $`SLATE_sslCertificate` set. Use of these options implicitly makes all connections to `slate-service` require the `https` scheme. 
- `--bootstrapUserFile` [$`SLATE_bootstrapUserFile`] specifies the path to the file from which the initial administrator account data is loaded if the Persistent Store must be initialized (default: 'slate_portal_user')
- `--encryptionKeyFile` [$`SLATE_encryptionKeyFile`] specifies the path to the file from which the encryption key used for storing secrets should be loaded (default: 'encryptionKey')
- `--appLoggingServerName` [$`SLATE_appLoggingServerName`] specifies the DNS name of the server to which installed application instances will be instructed to send monitoring information. If unspecified, monitoring will be disabled in each instance installed. 
- `--appLoggingServerPort` [$`SLATE_appLoggingServerPort`] specifies the port of the server to which installed application instances will be instructed to send monitoring information (default: 9200)
- `--config` [$`SLATE_config`] specifies the path to a file from which `slate-service` should read `key=value` pairs (one per line) for additional configuration settings, where `key` may be any of the valid options (without the leading dashes), including `config`. $`SLATE_config` is read after all other environment variables have been checked, so settings contained there will override environment variables. Config files specified with `--config` are parsed before further options, so settings contained there will take override preceding options, but will be overridden by subsequent options. `--config` may be specified multiple times (and `config` may appear as a key multiple times within a configuration file), each file so specified is parsed.
- `--allowAdHocApps` determines whether to allow SLATE application installs using the `--local` flag to provide a local chart. The default is `--allowAdHocApps=False`

If an SSL certificate is set, the files referred to by `--sslCertificate`/$`SLATE_sslCertificate` and `--sslKey`/$`SLATE_sslKey` must be readable by `slate-service`. 

## Running a local DynamoDB instance

For testing it is useful to run an instance of DynamoDB locally. See [the AWS documentation](https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/DynamoDBLocal.html) for details on obtaining the local version of Dynamo. Note that a reasonably new version of the JRE is required. The basic command to start Dynamo is

	java -Djava.library.path=./DynamoDBLocal_lib -jar DynamoDBLocal.jar
	
assuming it is being run from the directory in which the components have been unpacked. It may be useful to the database as a background process during testing. 
