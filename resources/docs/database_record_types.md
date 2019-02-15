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

VO Membership record

	ID: [string]<user ID>
	sortKey: [string]<user ID>:<VO ID>
	voID: [string]

## VO Table

VO record

	ID: [string]<VO ID>
	sortKey: [string]<VO ID>
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
	owningVO: [string]
	owningOrganization: [string]

Cluster Location record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>:Locations
	locations: [list[string]]

VO Access record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>:<VO ID>
	voID: [string]

VO Application Whitelist record

	ID: [string]<cluster ID>
	sortKey: [string]<cluster ID>:<VO ID>:Applications
	applications: [set[string]]

## Application Instance Table

Instance record

	ID: [string]<instance ID>
	sortKey: [string]<instance ID>
	name: [string]
	application: [string]
	owningVO: [string]
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
	vo: [string]
	cluster: [string]
	ctime: [string]
	contents: [string]
