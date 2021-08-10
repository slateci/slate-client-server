# SLATE systemd Notes

Both the SLATE production and development servers use `systemd` for managing the actual SLATE API service.
Unit configuration files for both servers can be found in this directory, as well as an additional DynamoDB file for running a local database.
This DynamoDB file is only used on the development server, because in production an externally managed database is used.
