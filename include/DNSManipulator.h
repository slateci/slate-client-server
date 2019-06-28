#ifndef SLATE_DNSMANIPULATOR_H
#define SLATE_DNSMANIPULATOR_H

#include <map>
#include <string>
#include <vector>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/route53/Route53Client.h>

class DNSManipulator{
public:
	DNSManipulator(const Aws::Auth::AWSCredentials& credentials, 
	               const Aws::Client::ClientConfiguration& clientConfig);
	
	///\return Whether this object is able to make DNS changes, because it is
	///        associated with a valid Rout53 server and account. 
	bool canUpdateDNS() const{ return validServer; }
	
	///Get the version of a record currently stored in Route53, 
	///which may not match actual DNS queries due to propagation delay.
	std::vector<std::string> getDNSRecord(Aws::Route53::Model::RRType type, const std::string& name) const;
	///Create a DNS record associating a name with an address
	bool setDNSRecord(const std::string& name, const std::string& address);
	///Delete the DNS record which associates a particular name with an address
	bool removeDNSRecord(const std::string& name, const std::string& address);
private:
	static std::string zoneForName(const std::string& name);
	
	bool safeToModifyDNS(const std::string& name, Aws::Route53::Model::RRType type) const;

	Aws::Route53::Route53Client dnsClient;
	std::map<std::string,std::string> hostedZones;
	bool validServer;
	
	const static std::string heritageTag;
};

#endif //SLATE_DNSMANIPULATOR_H