# Database record types

## User Table

User record

	ID: [string]<user ID>
	sortKey: [string]<user ID>
	name: [string]
	globusID: [string]
	token: [string]
	email: [string]
	phone: [string]
	institution: [string]
	admin: [bool]

Group Membership record

	ID: [string]<user ID>
	sortKey: [string]<user ID>:<Group ID>
	groupID: [string]

## Group Table

Group record

	ID: [string]<Group ID>
	sortKey: [string]<Group ID>
	name: [string]
	email: [string]
	phone: [string]
	scienceField: [string]
	description: [string]

## Cluster Table

Cluster record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>
	name: [string]
	config: [string]
	systemNamespace: [string]
	owningGroup: [string]
	owningOrganization: [string]
	monCredential: [string]

Cluster Location record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>:Locations
	locations: [list[string]]

Group Access record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>:<Group ID>
	groupID: [string]

Group Application Whitelist record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>:<Group ID>:Applications
	applications: [set[string]]

## Application Instance Table

Instance record

	ID: [string]<instance ID>
	sortKey: [string]<instance ID>
	name: [string]
	application: [string]
	owningGroup: [string]
	cluster: [string]
	ctime: [string]

Instance Configuration record

	ID: [string]<instance ID>
	sortKey: [string]<instance ID>:config
	config: [string]

## Secret Table

Secret record

	ID: [string]<secret ID>
	sortKey: [string]<secret ID>
	name: [string]
	owningGroup: [string]
	cluster: [string]
	ctime: [string]
	contents: [string]

## Monitoring Credential Table

Monitoring Credential record

	accessKey: [string]<access key>
	sortKey: [string]<access key>
	secretKey: [string]
	inUse: [bool]
	revoked: [bool]