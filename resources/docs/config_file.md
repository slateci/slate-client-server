# Config file parameters

The parameters for the configuration file that the server 
binary accepts is given below

| Parameter             | Type    | Notes                                                   | Default Value                               | 
|-----------------------|---------|---------------------------------------------------------|---------------------------------------------|
| awsAccessKey          | String  | Access key to AWS DynamoDB account being used by server |                                             | 
| awsSecretKey          | String  | Secret key to AWS DynamoDB account being used by server |                                             |
| awsRegion             | String  | AWS region hosting DynamoDB used by server              | us-east-1                                   |
| awsURLScheme          | String  | should be http or https                                 | http                                        |
| awsEndpoint           | String  | url to AWS endpoint                                     | localhost:8000                              | 
| baseDomain            | String  | base domain for generated dns subdomains                | slateci.net                                 |
| helmStableRepo        | String  | url to helm repo for stable charts                      | https://slateci.io/slate-catalog-stable/    |
| helmIncubatorRepo     | String  | url to helm repo for incubator charts                   | https://slateci.io/slate-catalog-incubator/ |
| openTelemetryEndpoint | String  | url to opentelemetry collector                          | ''                                          |
| disableTelemetry      | Boolean | enable opentelemetry?                                   | false                                       |
| disableSampling       | Boolean | only sample 50% of traces?                              | false                                       |
| serverInstance        | String  | name for server instance                                | SlateAPIServer-1                            | 
| serverEnvironment     | String  | name for server environment (e.g. development)          | dev                                         |
| geocodeEndpoint       | String  | url for geocoding service                               | https://geocode.xyz                         |
| geocodeToken          | String  | Token for geocode service                               |                                             |
| port                  | String  | Port that server will use                               | 18080                                       |
| sslCertificate        | String  | Path to ssl certificate file                            |                                             |
| sslKey                | String  | Path to ssl key file                                    |                                             |
| bootstrapUserFile     | String  | Path to file with user to bootstrap slate               | slate_portal_user                           |
| encryptionKeyFile     | String  | Path to file with encryption key that server will use   | encryptionKey                               |
| appLoggingServerName  | String  | Name to use when logging                                |                                             |
| appLoggingServerPort  | Integer | Port to use for app logging                             | 9200                                        |
| allowAdHocApps        | Boolean | Allow ad-hoc applications to be installed?              | false                                       |
| mailgunEndpoint       | String  | domain for mailgun endpoint                             | api.mailgun.net                             |
| mailgunKey            | String  | Key used to authenticate to mailgun                     |                                             |
| emailDomain           | String  | domain to use for outgoing emails                       | slateci.io                                  |
| opsEmail              | String  | email address to use for outgoing emails                | slateci-ops@googlegroups.com                |
| threads               | Integer | number of threads to run                                | 0                                           |

