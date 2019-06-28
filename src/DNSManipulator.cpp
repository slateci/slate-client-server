#include <DNSManipulator.h>

#include <Logging.h>

#include <aws/route53/Route53Client.h>
#include <aws/route53/model/ChangeResourceRecordSetsRequest.h>
#include <aws/route53/model/ListHostedZonesRequest.h>
#include <aws/route53/model/ListResourceRecordSetsRequest.h>
#include <aws/route53/model/ResourceRecordSet.h>
#include <aws/route53/model/ResourceRecord.h>
#include <aws/route53/model/RRType.h>
#include <aws/route53/model/TestDNSAnswerRequest.h>

DNSManipulator::DNSManipulator(const Aws::Auth::AWSCredentials& credentials, 
	                           const Aws::Client::ClientConfiguration& clientConfig)
:validServer(false){
	if(clientConfig.endpointOverride.find("amazonaws.com")!=std::string::npos){
		dnsClient=Aws::Route53::Route53Client(credentials);
		
		auto result=dnsClient.ListHostedZones(Aws::Route53::Model::ListHostedZonesRequest());
		if(!result.IsSuccess()){
			log_fatal("Failed to list hosted DNS zones: " 
			<< (int)result.GetError().GetErrorType() << ' '
			<< result.GetError().GetExceptionName() << ' '
			<< result.GetError().GetMessage());
		}
		
		for(auto zone : result.GetResult().GetHostedZones()){
			std::string id=zone.GetId();
			std::size_t pos=id.rfind('/');
			if(pos!=std::string::npos && pos<id.size())
				id=id.substr(pos+1);
			hostedZones.emplace(zone.GetName(),id);
		}
		
		validServer=true;
		log_info("DNS client ready");
	}
}

const std::string DNSManipulator::heritageTag="heritage=slate-api";

std::string DNSManipulator::zoneForName(const std::string& name){
	std::string zone="";
	std::size_t pos=name.rfind('.');
	if(pos!=std::string::npos && pos!=0){
		pos=name.rfind('.',pos-1);
		if(pos!=std::string::npos)
			zone=name.substr(pos+1)+".";
	}
	if(zone.empty())
		throw std::runtime_error("Unable to extract zone from "+name);
	return zone;
}

bool DNSManipulator::safeToModifyDNS(const std::string& name, Aws::Route53::Model::RRType type) const{
	auto zone=zoneForName(name);
	auto zoneIt=hostedZones.find(zone);
	if(zoneIt==hostedZones.end())
		throw std::runtime_error(zone+" is not a hosted zone in this AWS account");
	
	auto result=dnsClient.TestDNSAnswer(Aws::Route53::Model::TestDNSAnswerRequest()
	                                    .WithHostedZoneId(zoneIt->second)
	                                    .WithRecordName(name)
	                                    .WithRecordType(type)
	                                    );
	if(!result.IsSuccess()){
		throw std::runtime_error("Failed to look up A record for "+name+": "
		  +std::to_string((int)result.GetError().GetErrorType())+" "
		  +result.GetError().GetExceptionName()+" "
		  +result.GetError().GetMessage());
	}
	bool baseRecordExists=!result.GetResult().GetRecordData().empty();
		
	result=dnsClient.TestDNSAnswer(Aws::Route53::Model::TestDNSAnswerRequest()
	                               .WithHostedZoneId(zoneIt->second)
	                               .WithRecordName(name)
	                               .WithRecordType(Aws::Route53::Model::RRType::TXT));
	if(!result.IsSuccess()){
		throw std::runtime_error("Failed to look up TXT record for "+name+": "
		  +std::to_string((int)result.GetError().GetErrorType())+" "
		  +result.GetError().GetExceptionName()+" "
		  +result.GetError().GetMessage());
	}
	bool heritageRecordExists=!result.GetResult().GetRecordData().empty();
	if(heritageRecordExists){
		heritageRecordExists=false; //reset this prior to checking more exhaustively
		for(auto record : result.GetResult().GetRecordData()){
			if(record.find(heritageTag)!=std::string::npos)
				heritageRecordExists=true;
		}
	}
	
	//Now figure out what to do. Cases:
	//Base record exists, heritage record also exists -> We made this record and can replace it
	//Base record exists, heritage record does not exist -> Not our record, can not modify
	//Neither record exists -> We are free to create one
	//No base record, but heritage record exists 
	//    -> slightly corrupt state, but since we apparently touched the record in the past
	//       and no one else seems to be using it now, assume that we can replace it.
	log_info(name << ": " << (baseRecordExists?"has base record":"does not have base record")
	  << ", " << (heritageRecordExists?"has heritage record":"does not have heritage record"));
	return !baseRecordExists || heritageRecordExists;
}

std::vector<std::string> DNSManipulator::getDNSRecord(Aws::Route53::Model::RRType type, const std::string& name) const{
	if(!validServer)
		throw std::runtime_error("No valid Route53 server");

	auto zone=zoneForName(name);
	auto zoneIt=hostedZones.find(zone);
	if(zoneIt==hostedZones.end())
		throw std::runtime_error(zone+" is not a hosted zone in this AWS account");
	
	auto result=dnsClient.TestDNSAnswer(Aws::Route53::Model::TestDNSAnswerRequest()
	                                    .WithHostedZoneId(zoneIt->second)
	                                    .WithRecordName(name)
	                                    .WithRecordType(type)
	                                    );
	if(!result.IsSuccess()){
		throw std::runtime_error("Failed to look up "
		  +Aws::Route53::Model::RRTypeMapper::GetNameForRRType(type)
		  +" record for "+name+": "
		  +std::to_string((int)result.GetError().GetErrorType())+" "
		  +result.GetError().GetExceptionName()+" "
		  +result.GetError().GetMessage());
	}
	
	std::vector<std::string> data;
	data.reserve(result.GetResult().GetRecordData().size());
	for(auto record : result.GetResult().GetRecordData())
		data.push_back(record);
	return data;
}

bool DNSManipulator::setDNSRecord(const std::string& name, const std::string& address){
	if(!validServer)
		throw std::runtime_error("No valid Route53 server");
	
	//guess which kind of record we should be working on
	Aws::Route53::Model::RRType type;
	if(address.empty())
		throw std::runtime_error("Invalid IP address: must not be empty");
	//an IPv6 address must(?) contain at least one ':' and cannot match this
	else if(address.find_first_not_of("0123456789.")==std::string::npos)
		type=Aws::Route53::Model::RRType::A;
	//an IPv4 address in dot-decimal form must contain at least one '.' and cannot match this
	else if(address.find_first_not_of("0123456789abcdefABCDEF:")==std::string::npos)
		type=Aws::Route53::Model::RRType::AAAA;
	else
		throw std::runtime_error("Unrecognized IP address type: "+address);
	
	if(!safeToModifyDNS(name,type))
		return false;
	
	auto zone=zoneForName(name);
	auto zoneIt=hostedZones.find(zone);
	if(zoneIt==hostedZones.end())
		throw std::runtime_error(zone+" is not a hosted zone in this AWS account");
	
	auto mainRecordset=Aws::Route53::Model::ResourceRecordSet()
	                   .WithName(name)
	                   .WithType(type)
	                   .WithResourceRecords({Aws::Route53::Model::ResourceRecord().WithValue(address)})
	                   .WithTTL(300);
	auto txtRecordset=Aws::Route53::Model::ResourceRecordSet()
	                  .WithName(name)
	                  .WithType(Aws::Route53::Model::RRType::TXT)
	                  .WithResourceRecords({Aws::Route53::Model::ResourceRecord().WithValue('"'+heritageTag+'"')})
	                  .WithTTL(300);
	auto result=
	dnsClient.ChangeResourceRecordSets(Aws::Route53::Model::ChangeResourceRecordSetsRequest()
	  .WithChangeBatch(Aws::Route53::Model::ChangeBatch().WithChanges(
	  {Aws::Route53::Model::Change()
	   .WithAction(Aws::Route53::Model::ChangeAction::UPSERT)
	   .WithResourceRecordSet(mainRecordset),
	   Aws::Route53::Model::Change()
	   .WithAction(Aws::Route53::Model::ChangeAction::UPSERT)
	   .WithResourceRecordSet(txtRecordset)
	  }))
	  .WithHostedZoneId(zoneIt->second));
	if(!result.IsSuccess()){
		log_error("Failed set DNS records for " << name << ": "
		  << std::to_string((int)result.GetError().GetErrorType()) << " "
		  << result.GetError().GetExceptionName() << " "
		  << result.GetError().GetMessage());
		return false;
	}
	return true;
}

bool DNSManipulator::removeDNSRecord(const std::string& name, const std::string& address){
	if(!validServer)
		throw std::runtime_error("No valid Route53 server");

	//guess which kind of record we should be working on
	Aws::Route53::Model::RRType type;
	if(address.empty())
		throw std::runtime_error("Invalid IP address: must not be empty");
	//an IPv6 address must(?) contain at least one ':' and cannot match this
	else if(address.find_first_not_of("0123456789.")==std::string::npos)
		type=Aws::Route53::Model::RRType::A;
	//an IPv4 address in dot-decimal form must contain at least one '.' and cannot match this
	else if(address.find_first_not_of("0123456789abcdefABCDEF:")==std::string::npos)
		type=Aws::Route53::Model::RRType::AAAA;
	else
		throw std::runtime_error("Unrecognized IP address type: "+address);
	
	if(!safeToModifyDNS(name,type))
		return false;
	
	auto zone=zoneForName(name);
	auto zoneIt=hostedZones.find(zone);
	if(zoneIt==hostedZones.end())
		throw std::runtime_error(zone+" is not a hosted zone in this AWS account");
	
	auto mainRecordset=Aws::Route53::Model::ResourceRecordSet()
	                   .WithName(name)
	                   .WithType(type)
	                   .WithResourceRecords({Aws::Route53::Model::ResourceRecord().WithValue(address)})
	                   .WithTTL(300)
	                   ;
	auto txtRecordset=Aws::Route53::Model::ResourceRecordSet()
	                  .WithName(name)
	                  .WithType(Aws::Route53::Model::RRType::TXT)
	                  .WithResourceRecords({Aws::Route53::Model::ResourceRecord().WithValue('"'+heritageTag+'"')})
	                  .WithTTL(300)
	                  ;
	auto result=
	dnsClient.ChangeResourceRecordSets(Aws::Route53::Model::ChangeResourceRecordSetsRequest()
	  .WithChangeBatch(Aws::Route53::Model::ChangeBatch().WithChanges(
	  {Aws::Route53::Model::Change()
	   .WithAction(Aws::Route53::Model::ChangeAction::DELETE_)
	   .WithResourceRecordSet(mainRecordset),
	   Aws::Route53::Model::Change()
	   .WithAction(Aws::Route53::Model::ChangeAction::DELETE_)
	   .WithResourceRecordSet(txtRecordset)
	  }))
	  .WithHostedZoneId(zoneIt->second));
	if(!result.IsSuccess()){
		log_error("Failed remove DNS records for " << name << ": "
		  << std::to_string((int)result.GetError().GetErrorType()) << " "
		  << result.GetError().GetExceptionName() << " "
		  << result.GetError().GetMessage());
		return false;
	}
	return true;
}