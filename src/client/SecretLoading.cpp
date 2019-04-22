#include <client/SecretLoading.h>

#include <cerrno>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <CLI11.hpp>

#include <FileSystem.h>

namespace{

std::string getFileContents(const std::string& path){
	std::string contents;
	std::ifstream infile(path);
	if(!infile)
		throw CLI::ValidationError("Unable to open "+path+" for reading");
	std::istreambuf_iterator<char> iit(infile), end;
	std::copy(iit,end,std::back_inserter(contents));
	std::cout << "Read " << contents.size() << " bytes from " << path << std::endl;
	return contents;
}
	
std::string basename(const std::string& path){
	size_t slashIdx=path.rfind('/');
		if(slashIdx==std::string::npos)
			return path;
		if(slashIdx==path.size()-1)
			return("");
		return(path.substr(slashIdx+1));
}
	
} //anonymous namespace

bool validKey(const std::string& key){
	if(key.empty() || key.size()>253)
		return false;
	const static std::string allowedKeyCharacters="-._0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	if(key.find_first_not_of(allowedKeyCharacters)!=std::string::npos)
		return false;
	return true;
}

void parseFromFileSecretEntry(const std::string& arg, std::vector<std::string>& output){
	if(arg.empty())
		throw CLI::ValidationError("argument to --from-file must not be empty");
	try{
		if(is_directory(arg))
			return parseFromDirectorySecretEntry(arg,output);
	}catch(std::runtime_error&){
		//if stat failed then it wasn't a directory anyway
	}
	std::string inputPath, key;
	std::size_t equalPos=arg.find('=');
	key=arg.substr(0,equalPos);
	if(equalPos!=std::string::npos){
		if(equalPos==arg.size()-1)
			throw CLI::ValidationError("non-empty file path must follow equals in argument to --from-file");
		inputPath=arg.substr(equalPos+1);
	}
	else{
		inputPath=key;
		key=basename(key);
	}
	
	std::string value=getFileContents(inputPath);
	output.push_back(key+'='+value);
}

void parseFromDirectorySecretEntry(const std::string& arg, std::vector<std::string>& output){
	for(const auto& entry : directory(arg)){
		if(!is_directory(entry) && validKey(entry.path().name()))
			output.push_back(entry.path().name()+'='+getFileContents(entry.path()));
	}
}

void parseFromEnvFileSecretEntry(const std::string& arg, std::vector<std::string>& output){
	if(arg.empty())
		throw CLI::ValidationError("argument to --from-env-file must not be empty");
	
	std::ifstream infile(arg);
	if(!infile)
		throw CLI::ValidationError("Unable to open "+arg+" for reading");
	std::string line;
	std::size_t lineNum=1;
	while(std::getline(infile,line)){
		std::size_t equalPos=line.find('=');
		if(equalPos==std::string::npos)
			throw CLI::ValidationError("No key=value pair on line "+
			                           std::to_string(lineNum)+" of "+arg);
		std::string key=line.substr(0,equalPos);
		std::string value=line.substr(equalPos+1);
		if(!validKey(key))
			throw CLI::ValidationError("Invalid key on line "+
			                           std::to_string(lineNum)+" of "+arg);
		output.push_back(key+'='+value);
		lineNum++;
	}
}