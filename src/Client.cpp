#include "Client.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <zlib.h>

#include "client_version.h"
#include "Archive.h"
#include "Utilities.h"
#include "Process.h"
#include "OSDetection.h"

namespace{
///Get the path to the user's home directory
///\return home directory path, with a trailing slash
std::string getHomeDirectory(){
	std::string path;
	fetchFromEnvironment("HOME",path);
	if(path.empty())
		throw std::runtime_error("Unable to loacte home directory");
	if(path.back()!='/')
		path+='/';
	return path;
}
	
std::string makeTemporaryFile(const std::string& nameBase){
	std::string base=nameBase+"XXXXXXXX";
	//make a modifiable copy for mkstemp to scribble over
	std::unique_ptr<char[]> filePath(new char[base.size()+1]);
	strcpy(filePath.get(),base.c_str());
	struct fdHolder{
		int fd;
		~fdHolder(){ close(fd); }
	} fd{mkstemp(filePath.get())};
	if(fd.fd==-1){
		int err=errno;
		throw std::runtime_error("Creating temporary file failed with error " + std::to_string(err));
	}
	return filePath.get();
}
	
} //anonymous namespace

std::ostream& operator<<(std::ostream& os, const GeoLocation& gl){
	return os << gl.lat << ',' << gl.lon;
}

std::istream& operator>>(std::istream& is, GeoLocation& gl){
	is >> gl.lat;
	if(!is)
		return is;
	char comma=0;
	is >> comma;
	if(comma!=','){
		is.setstate(is.rdstate()|std::ios::failbit);
		return is;
	}
	is >> gl.lon;
	return is;
}

std::string Client::underline(std::string s) const{
	if(useANSICodes)
		return("\x1B[4m"+s+"\x1B[24m");
	return s;
}
std::string Client::bold(std::string s) const{
	if(useANSICodes)
		return("\x1B[1m"+s+"\x1B[22m");
	return s;
}

bool Client::clientShouldPrintOnlyJson() const{
        return outputFormat.substr(0,4) == "json";
}

//assumes that an introductory message has already been printed, without a newline
//attmepts to extract a JSON error message and prints it if successful
//always prints a conclusing newline.
void Client::showError(const std::string& maybeJSON){
	bool triggerVersionCheck=false;
	try{
		rapidjson::Document resultJSON;
		resultJSON.Parse(maybeJSON.c_str());
		if(resultJSON.IsObject() && resultJSON.HasMember("message")){
			std::cerr << ": " << resultJSON["message"].GetString();
			if(std::string(resultJSON["message"].GetString())=="Unsupported API version")
				triggerVersionCheck=true;
		}
		else if(!maybeJSON.empty())
			std::cerr << ": " << maybeJSON;
		else
			std::cerr << ": (empty response)";
	}catch(...){}
	std::cerr << std::endl;
	if(triggerVersionCheck)
		printVersion();
}


Client::ProgressManager::ProgressManager(): 
stop_(false),
showingProgress_(false),
thread_([this](){
  using std::chrono::system_clock;
  using duration=system_clock::time_point::duration;
  using mduration=std::chrono::duration<long long,std::milli>;
  duration sleepLen=std::chrono::duration_cast<duration>(mduration(1000));//one second
  system_clock::time_point nextTick=system_clock::now();
  nextTick+=sleepLen;
  
  WorkItem w;
  while(true){
    bool doNext=false;
    
    { //hold lock
      std::unique_lock<std::mutex> lock(this->mut_);
      //Wait for something to happen
      this->cond_.wait_until(lock,nextTick,
                             [this]{ return(this->stop_ || !this->work_.empty()); });
      //Figure out why we woke up
      if(this->stop_)
        return;
      //See if there's any work
      if(this->work_.empty()){
        //Nope. Back to sleep.
        nextTick+=sleepLen;
        continue;
      }
      //There is work. Should it be done now?
      nextTick=this->work_.top().time_;
      if(system_clock::now()>=nextTick){ //time to do it
        w=std::move(this->work_.top());
        this->work_.pop();
	//Update work to repeat action if repeat work is set to true
	if(this->repeatWork_)
	  this->ShowSomeProgress();
        //Update the time to next sleep until
        if(!this->work_.empty())
          nextTick=this->work_.top().time_;
        else
          nextTick+=sleepLen;
        doNext=true;
      }
    } //release lock
    if(doNext){
      w.work_();
      doNext=false;
    }
  }
})
{}

Client::ProgressManager::~ProgressManager(){
  {
    std::unique_lock<std::mutex> lock(mut_);
    stop_=true;
  }
  cond_.notify_all();
  thread_.join();
}

Client::ProgressManager::WorkItem::WorkItem(std::chrono::system_clock::time_point t,
                                  std::function<void()> w):
time_(t),work_(w){}

bool Client::ProgressManager::WorkItem::operator<(const Client::ProgressManager::WorkItem& other) const{
  return(time_<other.time_);
}

void Client::ProgressManager::start_scan_progress(std::string msg) {
	if(verbose_)
		std::cout << msg << std::endl;
}

void Client::ProgressManager::scan_progress(int progress) {
	if(verbose_)
		std::cout << progress << "% done..." << std::endl;
}

void Client::ProgressManager::show_progress() {
	if(verbose_)
		std::cout << "..." << std::endl;
}

void Client::ProgressManager::MaybeStartShowingProgress(std::string message){
  std::unique_lock<std::mutex> lock(this->mut_);
  if(!verbose_)
    return;
  
  if(!showingProgress_){
    //note when we got the request
    progressStart_=std::chrono::system_clock::now();
    showingProgress_=true;
    repeatWork_=false;
    progress_=0;
    //schedule actually showing the progress bar in 200 milliseconds
    using duration=std::chrono::system_clock::time_point::duration;
    using sduration=std::chrono::duration<long long,std::milli>;
    work_.emplace(progressStart_+std::chrono::duration_cast<duration>(sduration(200)),
                  [this,message]()->void{
                    std::unique_lock<std::mutex> lock(this->mut_);
                    if(this->showingProgress_){ //check for cancellation in the meantime!
                      this->start_scan_progress(message);
                      this->actuallyShowingProgress_=true;
                      if(progress_>0)
                        scan_progress(100*progress_);
                    }
                  });
    cond_.notify_all();
  }
  else{
    nestingLevel++;
  }
}

void Client::ProgressManager::ShowSomeProgress(){
  if (!verbose_)
    return;
  if(nestingLevel)
    return;
  using duration=std::chrono::system_clock::time_point::duration;
  using sduration=std::chrono::duration<long long,std::milli>;
  
  work_.emplace(std::chrono::system_clock::now()+std::chrono::duration_cast<duration>(sduration(2000)),
		[this]()->void{
		  repeatWork_ = true;
		  if(actuallyShowingProgress_){
		    std::unique_lock<std::mutex> lock(this->mut_);
		    show_progress();
		  }
		});
  cond_.notify_all();
}

void Client::ProgressManager::SetProgress(float value){
  if (!verbose_)
    return;
  if(nestingLevel)
    return;
  //ignore redundant values which would be displayed the same
  if((int)(100*value)==(int)(100*progress_))
    return;
  if(actuallyShowingProgress_){
    using duration=std::chrono::system_clock::time_point::duration;
    using sduration=std::chrono::duration<long long,std::milli>;

    work_.emplace(std::chrono::system_clock::now(),
		  [this, value]()->void{
		    progress_=value;
		    std::unique_lock<std::mutex> lock(this->mut_);
		    scan_progress(100*progress_);
		  });
    cond_.notify_all();
  }
}

void Client::ProgressManager::StopShowingProgress(){
  if (!verbose_)
    return;
  std::unique_lock<std::mutex> lock(this->mut_);
  if(nestingLevel){
    nestingLevel--;
    return;
  }
  if(showingProgress_){
    showingProgress_=false;
    actuallyShowingProgress_=false;
    repeatWork_=false;
    //while we're here with the lock held, remove pending start operations
    while(!work_.empty())
      work_.pop();
  }
}

std::string Client::formatTable(const std::vector<std::vector<std::string>>& items,
                                const std::vector<columnSpec>& columns,
				const bool headers) const{
	//try to determine to desired minimum width for every column
	//this will give wrong answers for multi-byte unicode sequences
	std::vector<std::size_t> minColumnWidths;
	for(std::size_t i=0; i<items.size(); i++){
		if(items[i].size()>minColumnWidths.size())
			minColumnWidths.resize(items[i].size(),0);
		for(std::size_t j=0; j<items[i].size(); j++)
			minColumnWidths[j]=std::max(minColumnWidths[j],items[i][j].size());
	}
	//figure out total size needed
	std::size_t totalWidth=0;
	for(std::size_t w : minColumnWidths)
		totalWidth+=w;
	std::size_t paddingWidth=(minColumnWidths.empty()?0:minColumnWidths.size()-1);
	totalWidth+=paddingWidth;
	
	if(totalWidth<=outputWidth){ //good case, everything fits
		std::ostringstream os;
		os << std::left;
		for(std::size_t i=0; i<items.size(); i++){
			for(std::size_t j=0; j<items[i].size(); j++){
				if(j)
					os << ' ';
				os << std::setw(minColumnWidths[j]+(useANSICodes && !i && headers?9:0)) 
				   << ((useANSICodes && i) || !headers?items[i][j]:underline(items[i][j]));
			}
			os << '\n';
		}
		return os.str();
	}
	else{
		//std::cout << "Table too wide: " << totalWidth << " columns when " << 
		//             outputWidth << " are allowed" << std::endl;
		
		//for now, try to shorten all columns which allow wrapping proportionally
		std::size_t wrappableWidth=0;
		for(unsigned int i=0; i<columns.size() && i<minColumnWidths.size(); i++){
			if(columns[i].allowWrap)
				wrappableWidth+=minColumnWidths[i];
		}
		//std::cout << "Wrappable width is " << wrappableWidth << std::endl;
		if(wrappableWidth>2){
			//determine a wrapping factor such that:
			//wrappableWidth*wrapFactor + (totalWidth-wrappableWidth) = outputWidth
			double wrapFactor=((double)outputWidth-(totalWidth-wrappableWidth))/wrappableWidth;
			//std::cout << "Wrap factor: " << wrapFactor << std::endl;
			//figure out which columns are the wrappable ones and how short they get
			for(unsigned int i=0; i<columns.size() && i<minColumnWidths.size(); i++){
				if(columns[i].allowWrap){
					minColumnWidths[i]=std::floor(minColumnWidths[i]*wrapFactor);
					if(!minColumnWidths[i])
						minColumnWidths[i]=1;
				}
			}
		}
		
		//whether the data in a given column is done for this row
		std::vector<bool> done(minColumnWidths.size(),false);
		//amount of each item which has been printed so far
		std::vector<std::size_t> printed(minColumnWidths.size(),false);
		
		//the identity function, because std::all_of requires a predicate
		auto id=[](bool b){return b;};
		
		std::ostringstream os;
		os << std::left;
		for(std::size_t i=0; i<items.size(); i++){
			//initially no column is done printing
			std::fill(done.begin(),done.end(),false);
			std::fill(printed.begin(),printed.end(),0);
			//need to continue until all columns are done
			while(!std::all_of(done.begin(),done.end(),id)){
				for(std::size_t j=0; j<items[i].size(); j++){
					if(j)
						os << ' ';
					if(done[j] && !std::all_of(done.begin()+j,done.end(),id)){
						os << std::setw(minColumnWidths[j]) << ' ';
						continue;
					}
					//figure out how much more of this column to print
					//start by assuming we will use up to the full column width
					std::size_t len_to_print=minColumnWidths[j];
					if(columns[j].allowWrap){
						//if this is a wrapped column, prefer to break after 
						//spaces and dashes.
						auto break_pos=items[i][j].find_first_of(" -_",printed[j]);
						while(break_pos!=std::string::npos && 
							  break_pos>=printed[j] && 
							  break_pos-printed[j]<minColumnWidths[j]){
							len_to_print=break_pos-printed[j]+1;
							break_pos=items[i][j].find_first_of(" -_",printed[j]+len_to_print);
						}
						//unless doing so would waste half or more of this line
						if(len_to_print*2<=minColumnWidths[j])
							len_to_print=minColumnWidths[j];
					}
					std::string to_print=items[i][j].substr(printed[j],len_to_print);
					
					if(j!=items[i].size()-1)
						os << std::setw(minColumnWidths[j]+(useANSICodes && !i?9:0));
					os << ((useANSICodes && i) || !headers?to_print:underline(to_print));
					
					if(printed[j]+len_to_print>=items[i][j].size()){
						done[j]=true;
					}
					else{
						printed[j]+=len_to_print;
					}
				}
				os << '\n';
			}
		}
		return os.str();
	}
}

std::string jsonValueToString(const rapidjson::Value& value){
	if(value.IsString())
		return value.GetString();
	if(value.IsNumber()){
		if(value.IsUint64()){
			uint64_t v=value.GetUint64();
			std::ostringstream ss;
			ss << v;
			return ss.str();
		}
		if(value.IsInt64()){
			int64_t v=value.GetInt64();
			std::ostringstream ss;
			ss << v;
			return ss.str();
		}
		if(value.IsDouble()){
			double v=value.GetDouble();
			std::ostringstream ss;
			ss << v;
			return ss.str();
		}
	}
	if(value.IsNull())
		return "Null";
	if(value.IsTrue())
		return "true";
	if(value.IsFalse())
		return "false";
	throw std::runtime_error("JSON value is not a scalar which can be displayed as a string");
}

std::string Client::jsonListToTable(const rapidjson::Value& jdata,
                                    const std::vector<columnSpec>& columns,
				    const bool headers = true) const{

	//When a list of Labels is given, find the label position to sort the columns by
	//Default to the first option if no option is found in the columnSpecs or labels

	int indexer = 0;	
	if (this->orderBy != ""){
			auto foundIt = std::find_if(columns.begin(),columns.end(),
			[this](const columnSpec& spec) -> bool {return spec.label == this->orderBy;});
			if (foundIt != columns.end()){
				indexer = std::distance(columns.begin(), foundIt);
			}
	}
	
	//Prepare the String vector for rows the decoded JSON object
	std::vector<std::vector<std::string>> data;
	
	//Load the headers to the vector of rows, to precede the data
	if (headers) {
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns)
			row.push_back(col.label);
	}
	
	//Decode the Json Object
	if(jdata.IsArray()){
		for(auto& jrow : jdata.GetArray()){
			data.emplace_back();
			auto& row=data.back();
			for(const auto& col : columns) {
				auto attribute = rapidjson::Pointer(col.attribute.c_str()).Get(jrow);
				if (!attribute)
					throw std::runtime_error("Given attribute does not exist");
				row.push_back(jsonValueToString(*attribute));
			}
		}
	}
	else if(jdata.IsObject()){
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns) {
			auto attribute = rapidjson::Pointer(col.attribute.c_str()).Get(jdata);
			if (!attribute)
				throw std::runtime_error("Given attribute does not exist");
			row.push_back(jsonValueToString(*attribute));
		}
	}

	int subsetIndex = headers ? 1 : 0;

	std::sort(
		data.begin() + subsetIndex, data.end(),
        	[indexer](const std::vector<std::string>& a, const std::vector<std::string>& b)
        	{ return a[indexer] < b[indexer]; }
	);

	return formatTable(data, columns, headers);
}

std::string readJsonPointer(const rapidjson::Value& jdata,
			    std::string pointer) {
	auto ptr = rapidjson::Pointer(pointer).Get(jdata);
	if (ptr == NULL)
		throw std::runtime_error("The pointer provided to format output is not valid");
	std::string result = ptr->GetString();
	return result + "\n";
}

std::string Client::formatOutput(const rapidjson::Value& jdata, const rapidjson::Value& original,
				 const std::vector<columnSpec>& columns) const{
	//output in json format
	if (this->clientShouldPrintOnlyJson()) {
		rapidjson::StringBuffer buf;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
		jdata.Accept(writer);
		std::string str = buf.GetString();
		str += "\n";
		return str;
	}

	//output in table format with custom columns given in a file
	if (outputFormat.find("custom-columns-file") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No file was specified to format output with custom columns");
	  
		std::string file = outputFormat.substr(outputFormat.find("=") + 1);
		if (file.empty())
			throw std::runtime_error("No file was specified to format output with custom columns");

		//read from given file to get columns
		std::ifstream columnFormat(file);
		if (!columnFormat.is_open())
			throw std::runtime_error("The specified file for custom columns was not able to be opened");
		
		std::string line;
		std::vector<columnSpec> customColumns;
		std::vector<std::string> labels;
		std::vector<std::string> data;
		//get labels from first line
		while (getline(columnFormat, line)) {
			//split words by tabs and/or spaces in each line
			std::stringstream ss(line);
			std::vector<std::string> tokens;
			std::string item;
			while (std::getline(ss, item, '\t')) {
				std::stringstream itemss(item);
				std::string separated;
				while (std::getline(itemss, separated, ' ')) { 
					if(!separated.empty())
						tokens.push_back(separated);
				}
			}

			//separate labels from the attribute for each label
			if (labels.size() == 0)
				labels = tokens;
			else if (data.size() == 0)
				data = tokens;
			else
				throw std::runtime_error("The custom columns file should only include labels and a single attribute for each label"); 
		}
		columnFormat.close();

		//create the custom columns from gathered labels & attributes
		for (auto i=0; i < labels.size(); i++) {
			columnSpec col(labels[i],data[i]);
			customColumns.push_back(col);
		}

		return jsonListToTable(jdata,customColumns);
	}
	
	//output in table format with custom columns given inline
	if (outputFormat.find("custom-columns") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No custom columns were specified to format output with");
	  
		std::string cols = outputFormat.substr(outputFormat.find("=") + 1);
		if (cols.empty())
			throw std::runtime_error("No custom columns were specified to format output with");

		//get columns from inline specification
		std::vector<columnSpec> customColumns;

		while (!cols.empty()) {
			if (cols.find(":") == std::string::npos)
				throw std::runtime_error("Every label for the table must have an attribute specified with it");

			std::string label = cols.substr(0, cols.find(":"));
			cols = cols.substr(cols.find(":") + 1);
			if (cols.empty())
				throw std::runtime_error("Every label for the table must have an attribute specified with it");
			
			std::string data;
			if (cols.find(",") != std::string::npos) {
				data = cols.substr(0, cols.find(","));
				cols = cols.substr(cols.find(",") + 1);
			} else {
				data = cols.substr(0);
				cols = "";
			}
			columnSpec col(label,data);
			customColumns.push_back(col);
		}
		return jsonListToTable(jdata,customColumns);
	}

	//output in default table format, with headers suppressed
	if (outputFormat == "no-headers")
		return jsonListToTable(jdata,columns,false);

	//output in format of a json pointer specified in the given file
	if (outputFormat.find("jsonpointer-file") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No json pointer file was specified to be used to format the output");

		std::string file = outputFormat.substr(outputFormat.find("=") + 1);
		if (file.empty())
			throw std::runtime_error("No file was specified to format output with");

		std::ifstream jsonpointer(file);
		if (!jsonpointer.is_open())
			throw std::runtime_error("The file specified to format output was unable to be opened");

		//get pointer specification from file
		std::string pointer;
		std::string part;
		while (getline(jsonpointer, part))
			pointer += part;
		std::string response = readJsonPointer(original, pointer);		
		jsonpointer.close();

		return response;
	}

	//output in format of json pointer specified inline
	if (outputFormat.find("jsonpointer") != std::string::npos) {
		if (outputFormat.find("=") == std::string::npos)
			throw std::runtime_error("No json pointer format was included to use to format the output");

		std::string jsonpointer = outputFormat.substr(outputFormat.find("=") + 1);
		if (jsonpointer.empty())
			throw std::runtime_error("No json pointer was given to format output");
		return readJsonPointer(original, jsonpointer);
	}
	
	//output in table format with default columns
	if (outputFormat.empty())
		return jsonListToTable(jdata,columns);

	throw std::runtime_error("Specified output format is not supported");
}

Client::Client(bool useANSICodes, std::size_t outputWidth):
apiVersion("v1alpha2"),
useANSICodes(useANSICodes),
outputWidth(outputWidth),
pman_()
{
	if(isatty(STDOUT_FILENO)){
		if(!this->outputWidth){ //determine width to use automatically
			struct winsize ws;
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
			this->outputWidth=ws.ws_col;
		}
		pman_.verbose_=true;
	}
	else
		this->useANSICodes=false;
}

void Client::setOutputWidth(std::size_t width){
	outputWidth=width;
}

void Client::setUseANSICodes(bool use){
	useANSICodes=use;
}

void Client::printVersion(){
	rapidjson::Document json(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = json.GetAllocator();
	rapidjson::Value client(rapidjson::kObjectType);
	client.AddMember("version", rapidjson::StringRef(clientVersionString), alloc);
	json.AddMember("client", client, alloc);
	
	std::vector<columnSpec> toPrint={{"Client Version", "/client/version"}};
	
	rapidjson::Document resultJSON;
	try{
		auto response=httpRequests::httpGet(getEndpoint()+"/version");
		if(response.status==200){
			resultJSON.Parse(response.body.c_str());
			rapidjson::Value server(rapidjson::kObjectType);
			if(resultJSON.HasMember("serverVersion")){
				server.AddMember("version", std::string(resultJSON["serverVersion"].GetString()), alloc);
				toPrint.push_back({"Server Version", "/server/version"});
			}
			if(resultJSON.HasMember("supportedAPIVersions") && resultJSON["supportedAPIVersions"].IsArray()){
				server.AddMember("apiVersions", resultJSON["supportedAPIVersions"], alloc);
			}
			json.AddMember("server", server, alloc);
		}
		else{
			std::cerr << "Failed to contact API server " << getEndpoint();
			showError(response.body);
		}
	}catch(...){
		std::cerr << "Failed to contact API server " << getEndpoint() << std::endl;
	}
	
	std::cout << formatOutput(json, json, toPrint);
	
	if (clientShouldPrintOnlyJson()){return;}

	if(json.HasMember("server") && json["server"].HasMember("apiVersions")){
		std::cout << "Server supported API versions:";
		bool foundMatchingVersion=false;
		for(const auto& item : json["server"]["apiVersions"].GetArray()){
			std::cout << ' ' << item.GetString();
			if(apiVersion==item.GetString())
				foundMatchingVersion=true;
		}
		std::cout << std::endl;
		if(!foundMatchingVersion){
			std::cout << bold("This client only supports SLATE API version "+
			                  apiVersion+"; it cannot work with this server") << std::endl;
		}
	}
}

void Client::upgrade(const upgradeOptions& options){
	//keep track of the os for which this client was built to download a matching build
#if BOOST_OS_LINUX
	const static std::string osName="linux";
#endif
#if BOOST_OS_MACOS
	const static std::string osName="macos";
#endif
#if BOOST_OS_BSD_FREE
	const static std::string osName="freebsd";
#endif
#if BOOST_OS_BSD_NET
	const static std::string osName="netbsd";
#endif
#if BOOST_OS_BSD_OPEN
	const static std::string osName="openbsd";
#endif
	unsigned long currentVersion=std::stoul(clientVersionString), availableVersion=0;
	
	//query central infrastructure for what the latest released version is
	const static std::string appcastURL="https://jenkins.slateci.io/artifacts/client/latest.json";
	ProgressToken progress(pman_,"Checking latest version...");
	auto versionResp=httpRequests::httpGet(appcastURL,defaultOptions());
	progress.end();
	if(versionResp.status!=200){
		throw std::runtime_error("Unable to contact "+appcastURL+ 
		                         " to get latest version information; error "+
		                         std::to_string(versionResp.status));
		return;
	}
	rapidjson::Document resultJSON;
	std::string availableVersionString;
	std::string downloadURL;
	try{
		resultJSON.Parse(versionResp.body.c_str());
		if(!resultJSON.IsArray() || !resultJSON.GetArray().Size())
			throw std::runtime_error("JSON document should be a non-empty array");
		//for now we only look at the last entry in the array
		const auto& versionEntry=resultJSON.GetArray()[resultJSON.GetArray().Size()-1];
		if(!versionEntry.IsObject() 
		   || !versionEntry.HasMember("version") || !versionEntry.HasMember("platforms")
		   || !versionEntry["version"].IsString() || !versionEntry["platforms"].IsObject())
			throw std::runtime_error("Version entry does not have expected structure");
		availableVersionString=versionEntry["version"].GetString();
		if(versionEntry["platforms"].HasMember(osName)){
			if(!versionEntry["platforms"][osName].IsString())
				throw std::runtime_error("Expected OS name to map to a download URL");
			downloadURL=versionEntry["platforms"][osName].GetString();
		}
	}catch(std::exception& err){
		throw std::runtime_error("Failed to parse new version description: "
		                         +std::string(err.what()));
	}catch(...){
		throw std::runtime_error("Build server returned invalid JSON");
	}
	try{
		availableVersion=std::stoul(availableVersionString);
	}catch(std::runtime_error& err){
		throw std::runtime_error("Unable to parse available version string for comparison");
	}
	if(availableVersion<=currentVersion){
		std::cout << "This executable is up-to-date" << std::endl;
		return;
	}
	std::cout << "Version " << availableVersionString 
	<< " is available; this executable is version " << clientVersionString << std::endl;
	if(downloadURL.empty())
		throw std::runtime_error("No build is available for this platform");
	std::cout << "Do you want to download and install the new version? [Y/n] ";
	std::cout.flush();
	if(!options.assumeYes){
		HideProgress quiet(pman_);
		std::string answer;
		std::getline(std::cin,answer);
		if(answer!="" && answer!="y" && answer!="Y")
			throw std::runtime_error("Installation cancelled");
	}
	else
		std::cout << "assuming yes" << std::endl;
	
	//download the new version
	progress.start("Downloading latest version...");
	auto response=httpRequests::httpGet(downloadURL,defaultOptions());
	progress.end();
	if(response.status!=200)
		throw std::runtime_error("Failed to download new version archive: error "+std::to_string(response.status));
	//decompress and extract from gzipped tarball
	std::istringstream compressed(response.body);
	std::stringstream decompressed;
	gzipDecompress(compressed, decompressed);
	//extractFromUStar(decompressed,"slate",tmpLoc);
	TarReader tr(decompressed);
	auto tmpLoc=makeTemporaryFile("");
	std::ofstream outfile(tmpLoc);
	auto datastream=tr.streamForFile("slate");
	std::copy(std::istreambuf_iterator<char>(datastream->rdbuf()),
	          std::istreambuf_iterator<char>(),
	          std::ostreambuf_iterator<char>(outfile));
	outfile.close();
	int res=chmod(tmpLoc.c_str(),tr.modeForFile("slate"));
	if(res!=0){
		res=errno;
		throw std::runtime_error("Failed to set mode of new executable: error "+std::to_string(res));
	}
	//this step overwrites the current executable if successful!
	res=rename(tmpLoc.c_str(),program_location().c_str());
	if(res!=0){
		res=errno;
		throw std::runtime_error("Failed to replace current executable with new version: error "+std::to_string(res));
	}
	std::cout << "Upgraded to version " << availableVersionString << std::endl;
}

void Client::createVO(const VOCreateOptions& opt){
	ProgressToken progress(pman_,"Creating VO...");
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
  
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", rapidjson::StringRef(opt.voName.c_str()), alloc);
	metadata.AddMember("scienceField", rapidjson::StringRef(opt.scienceField.c_str()), alloc);
	request.AddMember("metadata", metadata, alloc);
  
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);
  
	auto response=httpRequests::httpPost(makeURL("vos"),buffer.GetString(),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Successfully created VO " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to create VO " << opt.voName;
		showError(response.body);
	}
}

void Client::updateVO(const VOUpdateOptions& opt){
	if(opt.email.empty() && opt.phone.empty() && opt.scienceField.empty() && opt.description.empty()){
		std::cout << "No updates specified" << std::endl;
		return;
	}

	ProgressToken progress(pman_,"Updating VO...");
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", rapidjson::StringRef(opt.voName.c_str()), alloc);
	if(!opt.email.empty())
		metadata.AddMember("email", rapidjson::StringRef(opt.email.c_str()), alloc);
	if(!opt.phone.empty())
		metadata.AddMember("phone", rapidjson::StringRef(opt.phone.c_str()), alloc);
	if(!opt.scienceField.empty())
		metadata.AddMember("scienceField", rapidjson::StringRef(opt.scienceField.c_str()), alloc);
	if(!opt.description.empty())
		metadata.AddMember("description", rapidjson::StringRef(opt.description.c_str()), alloc);
	request.AddMember("metadata", metadata, alloc);
	
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);
	
	auto response=httpRequests::httpPut(makeURL("vos/"+opt.voName),buffer.GetString(),defaultOptions());
	if(response.status==200)
		std::cout << "Successfully updated VO " << opt.voName << std::endl;
	else{
		std::cerr << "Failed to update VO " << opt.voName;
		showError(response.body);
	}
}

void Client::deleteVO(const VODeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting VO...");
	auto response=httpRequests::httpDelete(makeURL("vos/"+opt.voName),defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted VO " << opt.voName << std::endl;
	else{
		std::cerr << "Failed to delete VO " << opt.voName;
		showError(response.body);
	}
}

void Client::getVOInfo(const VOInfoOptions& opt){
	ProgressToken progress(pman_,"Fetching VO info...");
	auto url = makeURL("vos/"+opt.voName);
	auto response=httpRequests::httpGet(url,defaultOptions());
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json, json, 
		                          {{"Name", "/metadata/name"},
		                           {"Field", "/metadata/scienceField", true},
		                           {"Email", "/metadata/email", true},
		                           {"Phone", "/metadata/phone", true},
		                           {"ID", "/metadata/id", true}
		                          });
		if(clientShouldPrintOnlyJson()){return;}
		std::cout << "Description: " << json["metadata"]["description"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to get information about VO " << opt.voName;
		showError(response.body);
	}
}

void Client::listVOs(const VOListOptions& opt){
	ProgressToken progress(pman_,"Fetching VO list...");
	auto url = makeURL("vos");
	if (opt.user)
		url += "&user=true";
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", "/metadata/name"},{"ID", "/metadata/id", true}});
	}
	else{
		std::cerr << "Failed to list VOs";
		showError(response.body);
	}
}

std::string Client::extractClusterConfig(std::string configPath, bool assumeYes){
	const static std::string controllerRepo="https://gitlab.com/ucsd-prp/nrp-controller";
	const static std::string controllerDeploymentURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/deploy.yaml";
	const static std::string federationRoleURL="https://gitlab.com/ucsd-prp/nrp-controller/raw/master/federation-role.yaml";
	
	//find the config information
	if(configPath.empty()) //try environment
		fetchFromEnvironment("KUBECONFIG",configPath);
	if(configPath.empty()) //try stardard default path
		configPath=getHomeDirectory()+".kube/config";

	if(checkPermissions(configPath)==PermState::DOES_NOT_EXIST)
		throw std::runtime_error("Config file '"+configPath+"' does not exist");
	
	std::cout << "Extracting kubeconfig from " << configPath << "..." << std::endl;
	auto result = runCommand("kubectl",{"config","view","--minify","--flatten","--kubeconfig",configPath});

	if(result.status)
		throw std::runtime_error("Unable to extract kubeconfig: "+result.error);
	//config=result.output;
       
	//Try to figure out whether we are inside of a federation cluster, or 
	//otherwise whether the federation controller is deployed
	std::cout << "Checking for privilege level/deployment controller status..." << std::endl;
	result=runCommand("kubectl",{"get","deployments","-n","kube-system","--kubeconfig",configPath});
	if(result.status==0){
		//We can list objects in kube-system, so permissions are too broad. 
		//Check whether the controller is running:
		if(result.output.find("nrp-controller")==std::string::npos){
			//controller is not deployed, 
			//check whether the user wants us to install it
			std::cout << "It appears that the nrp-controller is not deployed on this cluster.\n\n"
			<< "The nrp-controller is a utility which allows SLATE to operate with\n"
			<< "reduced privileges in your Kubernetes cluster. It grants SLATE access to a\n"
			<< "single initial namespace of your choosing and a mechanism to create additional\n"
			<< "namespaces, without granting it any access to namespaces it has not created.\n"
			<< "This means that you can be certain that SLATE will not interfere with other\n"
			<< "uses of your cluster.\n"
			<< "See " << controllerRepo << " for more information on the\n"
			<< "controller software and \n"
			<< controllerDeploymentURL << " for the\n"
			<< "deployment definition used to install it.\n\n"
			<< "This component is needed for SLATE to use this cluster.\n"
			<< "Do you want to install it now? [y]/n: ";
			std::cout.flush();
			
			if(!assumeYes){
				HideProgress quiet(pman_);
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
			
			std::cout << "Applying " << controllerDeploymentURL << std::endl;
			result=runCommand("kubectl",{"apply","-f",controllerDeploymentURL,"--kubeconfig",configPath});
			if(result.status)
				throw std::runtime_error("Failed to deploy federation controller: "+result.error);
			std::cout << "Waiting for Custom Resource Definitions to become active..." << std::endl;
			while(true){
				result=runCommand("kubectl",{"get","crds"});
				if(result.output.find("clusters.nrp-nautilus.io")!=std::string::npos &&
				   result.output.find("clusternamespaces.nrp-nautilus.io")!=std::string::npos)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		else
			std::cout << " Controller is deployed" << std::endl;

		pman_.SetProgress(0.2);
		
		std::cout << "Checking for federation ClusterRole..." << std::endl;
		result=runCommand("kubectl",{"get","clusterrole","federation-cluster","--kubeconfig",configPath});
		if(result.status){
			std::cout << "It appears that the federation-cluster ClusterRole is not deployed on this cluster.\n\n"
			<< "This is a ClusterRole used by the nrp-controller to grant SLATE access\n"
			<< "to only its own namespaces. You can view its definition at\n"
			<< federationRoleURL << ".\n\n"
			<< "This component is needed for SLATE to use this cluster.\n"
			<< "Do you want to install it now? [y]/n: ";
			std::cout.flush();
			
			if(!assumeYes){
				HideProgress quiet(pman_);
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
			
			std::cout << "Applying " << federationRoleURL << std::endl;
			result=runCommand("kubectl",{"apply","-f",federationRoleURL,"--kubeconfig",configPath});
			if(result.status)
				throw std::runtime_error("Failed to deploy federation clusterrole: "+result.error);
		}
		else
			std::cout << " ClusterRole is defined" << std::endl;
		
		//At this pont we have ensured that we have the right tools, but the 
		//priveleges are still too high. 
		std::cout << "SLATE should be granted access using a ServiceAccount created with a Cluster\n"
		<< "object by the nrp-controller. Do you want to create such a ServiceAccount\n"
		<< "automatically now? [y]/n: ";
		std::cout.flush();
		if(!assumeYes){
			HideProgress quiet(pman_);
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="" && answer!="y" && answer!="Y")
				throw std::runtime_error("Cluster registration aborted");
		}
		else
			std::cout << "assuming yes" << std::endl;

		std::string namespaceName;
		std::cout << "Please enter the name you would like to give the ServiceAccount and core\n"
		<< "SLATE namespace. The default is 'slate-system': ";
		if(!assumeYes){
			HideProgress quiet(pman_);
			std::cout.flush();
			std::getline(std::cin,namespaceName);
		}
		else
			std::cout << "assuming slate-system" << std::endl;
		if(namespaceName.empty())
			namespaceName="slate-system";
		//check whether the selected namespace/cluster already exists
		result=runCommand("kubectl",{"get","cluster",namespaceName,"-o","name"});
		if(result.status==0 && result.output.find("cluster.nrp-nautilus.io/"+namespaceName)!=std::string::npos){
			std::cout << "The namespace '" << namespaceName << "' already exists.\n"
			<< "Proceed with reusing it? [y]/n: ";
			std::cout.flush();
			if(!assumeYes){
				HideProgress quiet(pman_);
				std::string answer;
				std::getline(std::cin,answer);
				if(answer!="" && answer!="y" && answer!="Y")
					throw std::runtime_error("Cluster registration aborted");
			}
			else
				std::cout << "assuming yes" << std::endl;
		}
		else{
			std::cout << "Creating Cluster '" << namespaceName << "'..." << std::endl;
			result=runCommandWithInput("kubectl",
R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: Cluster
metadata: 
  name: )"+namespaceName,
									   {"create","-f","-"});
			if(result.status)
				throw std::runtime_error("Cluster creation failed: "+result.error);
		}
		
		//wait for the corresponding namespace to be ready
		while(true){
			result=runCommand("kubectl",{"get","namespace",namespaceName,"-o","jsonpath={.status.phase}"});
			if(result.status==0 && result.output=="Active")
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		pman_.SetProgress(0.5);
		
		std::cout << "Locating ServiceAccount credentials..." << std::endl;
		result=runCommand("kubectl",{"get","serviceaccount",namespaceName,"-n",namespaceName,"-o","jsonpath='{.secrets[].name}'"});
		if(result.status)
			throw std::runtime_error("Unable to locate ServiceAccount credential secret: "+result.error);
		std::string credName=result.output;
		{ //kubectl leaves quotes around the name. Get rid of them.
			std::size_t pos;
			while((pos=credName.find('\''))!=std::string::npos)
				credName.erase(pos,1);
		}

		pman_.SetProgress(0.6);
		
		std::cout << "Extracting CA data..." << std::endl;
		result=runCommand("kubectl",{"get","secret",credName,"-n",namespaceName,"-o","jsonpath='{.data.ca\\.crt}'"});
		if(result.status)
			throw std::runtime_error("Unable to extract ServiceAccount CA data from secret"+result.error);
		std::string caData=result.output;

		pman_.SetProgress(0.7);
		
		std::cout << "Determining server address..." << std::endl;
		result=runCommand("kubectl",{"cluster-info"});
		if(result.status)
			throw std::runtime_error("Unable to get Kubernetes cluster-info: "+result.error);
		std::string serverAddress;
		{
			auto startPos=result.output.find("http");
			if(startPos==std::string::npos)
				throw std::runtime_error("Unable to parse Kubernetes cluster-info");
			auto endPos=result.output.find((char)0x1B,startPos);
			if(endPos==std::string::npos)
				throw std::runtime_error("Unable to parse Kubernetes cluster-info");
			serverAddress=result.output.substr(startPos,endPos-startPos);
		}

		pman_.SetProgress(0.8);
		
		std::cout << "Extracting ServiceAccount token..." << std::endl;
		result=runCommand("kubectl",{"get","secret","-n",namespaceName,credName,"-o","jsonpath={.data.token}"});
		if(result.status)
			throw std::runtime_error("Unable to extract ServiceAccount token data from secret: "+result.error);
		std::string token=decodeBase64(result.output);
		
		std::ostringstream os;
		os << R"(apiVersion: v1
clusters:
- cluster:
    certificate-authority-data: )"
		<< caData << '\n'
		<< "    server: " << serverAddress << '\n'
		<< R"(  name: )" << namespaceName << R"(
contexts:
- context:
    cluster: )" << namespaceName << R"(
    namespace: )" << namespaceName << '\n'
		<< "    user: " << namespaceName << '\n'
		<< R"(  name: )" << namespaceName << R"(
current-context: )" << namespaceName << R"(
kind: Config
preferences: {}
users:
- name: )" << namespaceName << '\n'
		<< R"(  user:
    token: )" << token << '\n';
		std::cout << " Done generating config with limited privileges" << std::endl;
		
		return os.str();
	}
	else{
		throw std::runtime_error("Unable to list deployments in the kube-system namespace; "
		                         "this command needs to be run with kubernetes administrator "
		                         "privileges in order to create the correct environment (with "
		                         "limited privileges) for SLATE to use.\n"
		                         "Kubernetes error: "+result.error);
	}
}

void Client::createCluster(const ClusterCreateOptions& opt){
	ProgressToken progress(pman_,"Creating cluster...");
	
	//This is a lengthy operation, and we don't actually talk to the API server 
	//until the end. Check now that the user has some credentials (although we 
	//cannot assess validity) in order to fail early in the common case of the 
	//user forgetting to install a token
	(void)getToken();

	std::string config=extractClusterConfig(opt.kubeconfig,opt.assumeYes);
	
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.clusterName, alloc);
	metadata.AddMember("vo", opt.voName, alloc);
	metadata.AddMember("organization", opt.orgName, alloc);
	metadata.AddMember("kubeconfig", config, alloc);
	request.AddMember("metadata", metadata, alloc);
        
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	pman_.SetProgress(0.9);
	
	std::cout << "Sending config to SLATE server..." << std::endl;
	auto response=httpRequests::httpPost(makeURL("clusters"),buffer.GetString(),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully created cluster " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to create cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::updateCluster(const ClusterUpdateOptions& opt){
	ProgressToken progress(pman_,"Updating cluster...");
	
	//This is a (potentially) lengthy operation, and we don't actually talk to the API server 
	//until the end. Check now that the user has some credentials (although we 
	//cannot assess validity) in order to fail early in the common case of the 
	//user forgetting to install a token
	(void)getToken();
	
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	if(!opt.orgName.empty())
		metadata.AddMember("organization", opt.orgName, alloc);
	if(opt.reconfigure || !opt.kubeconfig.empty()){
		std::string config=extractClusterConfig(opt.kubeconfig,opt.assumeYes);
		metadata.AddMember("kubeconfig", config, alloc);
	}
	if(!opt.locations.empty()){
		rapidjson::Value clusterLocation(rapidjson::kArrayType);
		clusterLocation.Reserve(opt.locations.size(), alloc);
		for(const auto& location : opt.locations){
			rapidjson::Value entry(rapidjson::kObjectType);
			entry.AddMember("lat",location.lat, alloc);
			entry.AddMember("lon",location.lon, alloc);
			clusterLocation.PushBack(entry, alloc);
		}
		metadata.AddMember("location", clusterLocation, alloc);
	}
	request.AddMember("metadata", metadata, alloc);
        
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	pman_.SetProgress(0.9);
	
	std::cout << "Sending config to SLATE server..." << std::endl;
	auto response=httpRequests::httpPut(makeURL("clusters/"+opt.clusterName),buffer.GetString(),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		//rapidjson::Document resultJSON;
		//resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully updated cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to update cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::deleteCluster(const ClusterDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting cluster...");
	auto response=httpRequests::httpDelete(makeURL("clusters/"+opt.clusterName),defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted cluster " << opt.clusterName << std::endl;
	else{
		std::cerr << "Failed to delete cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::listClusters(const ClusterListOptions& opt){
  	std::string url=makeURL("clusters");
	if(!opt.vo.empty())
		url+="&vo="+opt.vo;
	ProgressToken progress(pman_,"Fetching cluster list...");
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             {{"Name","/metadata/name"},
		                              {"Admin","/metadata/owningVO"},
		                              {"ID","/metadata/id",true}});
	}
	else{
		std::cerr << "Failed to list clusters";
		showError(response.body);
	}
}

void Client::getClusterInfo(const ClusterInfoOptions& opt){
	auto url = makeURL("clusters/"+opt.clusterName);
	ProgressToken progress(pman_,"Fetching cluster info...");
	auto response=httpRequests::httpGet(url,defaultOptions());
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json, json, 
		                          {{"Name", "/metadata/name"},
		                           {"Admin","/metadata/owningVO"},
		                           {"Owner","/metadata/owningOrganization"},
		                           {"ID", "/metadata/id", true}
		                          });
		if(clientShouldPrintOnlyJson()){return;}
		if(json["metadata"].HasMember("location") && json["metadata"]["location"].IsArray()
		  && json["metadata"]["location"].GetArray().Size()>0){
			std::cout << '\n' << formatOutput(json["metadata"]["location"],
			                          json["metadata"]["location"],
			                          {{"Latitude","/lat"},{"Longitude","/lon"}});
		}
	}
	else{
		std::cerr << "Failed to get information about cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::grantVOClusterAccess(const VOClusterAccessOptions& opt){
	ProgressToken progress(pman_,"Granting VO cluster access...");
	auto response=httpRequests::httpPut(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_vos/"
	                                            +opt.voName),"",defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully granted VO " << opt.voName 
		          << " access to cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to grant VO " << opt.voName << " access to cluster " 
		          << opt.clusterName;
		showError(response.body);
	}
}

void Client::revokeVOClusterAccess(const VOClusterAccessOptions& opt){
	ProgressToken progress(pman_,"Removing VO cluster access...");
	auto response=httpRequests::httpDelete(makeURL("clusters/"
	                                               +opt.clusterName
	                                               +"/allowed_vos/"
	                                               +opt.voName),
	                                       defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully revoked VO " << opt.voName 
		          << " access to cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to revoke VO " << opt.voName << " access to cluster " 
		          << opt.clusterName;
		showError(response.body);
	}
}

void Client::listVOWithAccessToCluster(const ClusterAccessListOptions& opt){
	ProgressToken progress(pman_,"Fetching VOs with cluster access...");
	auto response=httpRequests::httpGet(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_vos"),
	                                    defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", "/metadata/name"},
		      {"ID", "/metadata/id", true}});
	}
	else{
		std::cerr << "Failed to retrieve VOs with access to cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::listAllowedApplications(const VOClusterAppUseListOptions& opt){
	ProgressToken progress(pman_,"Fetching allowed application list...");
	auto response=httpRequests::httpGet(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_vos/"
	                                            +opt.voName
	                                            +"/applications"),
	                                    defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", ""}});
	}
	else{
		std::cerr << "Failed to retrieve VOs with access to cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::allowVOUseOfApplication(const VOClusterAppUseOptions& opt){
	ProgressToken progress(pman_,"Giving VO access to use application...");
	auto response=httpRequests::httpPut(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_vos/"
	                                            +opt.voName
	                                            +"/applications/"
	                                            +opt.appName),"",defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully granted VO " << opt.voName 
		          << " permission to use " << opt.appName 
		          << " on cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to grant VO " << opt.voName << " permission to use " 
		          << opt.appName << " on cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::denyVOUseOfApplication(const VOClusterAppUseOptions& opt){
	ProgressToken progress(pman_,"Removing VO access to use application...");
	auto response=httpRequests::httpDelete(makeURL("clusters/"
	                                               +opt.clusterName
	                                               +"/allowed_vos/"
	                                               +opt.voName
	                                               +"/applications/"
	                                               +opt.appName),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully removed VO " << opt.voName 
		          << " permission to use " << opt.appName 
		          << " on cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to remove VO " << opt.voName << " permission to use " 
		          << opt.appName << " on cluster " << opt.clusterName;
		showError(response.body);
	}
}

void Client::listApplications(const ApplicationOptions& opt){
  	ProgressToken progress(pman_,"Listing applications...");
	std::string url=makeURL("apps");
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             {{"Name","/metadata/name"},
		                              {"App Version","/metadata/app_version"},
		                              {"Chart Version","/metadata/chart_version"},
		                              {"Description","/metadata/description",true}});
	}
	else{
		std::cerr << "Failed to list applications";
		showError(response.body);
	}
}
	
void Client::getApplicationConf(const ApplicationConfOptions& opt){
	ProgressToken progress(pman_,"Fetching application configuration...");
	std::string url=makeURL("apps/"+opt.appName);
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";

	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::string configuration=resultJSON["spec"]["body"].GetString();
		//if the user specified a file, write there
		if(!opt.outputFile.empty()){
			std::ofstream confFile(opt.outputFile);
			if(!confFile)
				throw std::runtime_error("Unable to write configuration to "+opt.outputFile);
			confFile << configuration;
		}
		else //otherwise print to stdout
			std::cout << configuration << std::endl;
	}
	else{
		std::cerr << "Failed to get configuration for application " << opt.appName;
		showError(response.body);
	}
}
	
void Client::installApplication(const ApplicationInstallOptions& opt){
	ProgressToken progress(pman_,"Installing application...");
	
	//figure out whether we are trying to directly install a chart
	bool directChart=false;
	{
		struct stat data;
		int err=stat(opt.appName.c_str(),&data);
		if(err!=0){
			err=errno;
			if(err!=ENOENT)
				throw std::runtime_error("Unable to stat "+opt.appName);
			else{
				//doesn't exist, so not a local chart				
			}
		}
		else{
			//TODO: verify that the file is a directory, has the structure 
			//of a helm chart, etc.
			directChart=true;
		}
	}
	
	std::string configuration;
	if(!opt.configPath.empty()){
		//read in user-specified configuration
		std::ifstream confFile(opt.configPath);
		if(!confFile)
			throw std::runtime_error("Unable to read application instance configuration from "+opt.configPath);
		std::string line;
		while(std::getline(confFile,line))
			configuration+=line+"\n";
	}

	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	request.AddMember("vo", rapidjson::StringRef(opt.vo.c_str()), alloc);
	request.AddMember("cluster", rapidjson::StringRef(opt.cluster.c_str()), alloc);
	request.AddMember("configuration", rapidjson::StringRef(configuration.c_str()), alloc);
	if(directChart){
		std::stringstream tarBuffer,gzipBuffer;
		TarWriter tw(tarBuffer);
		std::string dirPath=opt.appName;
		while(dirPath.size()>1 && dirPath.back()=='/') //strip trailing slashes
			dirPath=dirPath.substr(0,dirPath.size()-1);
		recursivelyArchive(dirPath,tw,true);
		tw.endStream();
		gzipCompress(tarBuffer,gzipBuffer);
		std::string encodedChart=encodeBase64(gzipBuffer.str());
		request.AddMember("chart",encodedChart,alloc);
	}
	
	std::string url;
	if(directChart)
		url=makeURL("apps/ad-hoc");
	else
		url=makeURL("apps/"+opt.appName);
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(url,buffer.GetString(),defaultOptions());
	
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Successfully installed application " 
			  << resultJSON["metadata"]["application"].GetString() << " as instance "
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to install application " << opt.appName;
		showError(response.body);
	}
}

void Client::listInstances(const InstanceListOptions& opt){
	ProgressToken progress(pman_,"Fetching application instance list...");
	std::string url=makeURL("instances");

	std::vector<columnSpec> columns;
	if (opt.vo.empty() && opt.cluster.empty())
		columns = {{"Name","/metadata/name"},
			   {"VO","/metadata/vo"},
			   {"Cluster","/metadata/cluster"},
			   {"ID","/metadata/id",true}};
	
	if(!opt.vo.empty()) {
		url+="&vo="+opt.vo;
		columns = {{"Name","/metadata/name"},
			   {"Cluster","/metadata/cluster"},
			   {"ID","/metadata/id",true}};
	}
	if(!opt.cluster.empty()) {
		url+="&cluster="+opt.cluster;
		columns = {{"Name","/metadata/name"},
			   {"VO","/metadata/vo"},
			   {"ID","/metadata/id",true}};
	}
	if (!opt.cluster.empty() && !opt.vo.empty())
		columns = {{"Name","/metadata/name"},
			   {"ID","/metadata/id",true}};
	
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		filterInstanceNames(json, "/items");
		std::cout << formatOutput(json["items"], json,
		                             columns);
	}
	else{
		std::cerr << "Failed to list application instances";
		showError(response.body);
	}
}

void Client::getInstanceInfo(const InstanceOptions& opt){
	ProgressToken progress(pman_,"Fetching instance information...");
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance info command requires an instance ID, not a name");
	
	std::string url=makeURL("instances/"+opt.instanceID)+"&detailed";
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		filterInstanceNames(body,"");
		std::cout << formatOutput(body, body,
		                             {{"Name","/metadata/name"},
		                              {"Started","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
	
		if(clientShouldPrintOnlyJson()){return;}
	
		std::cout << '\n' << bold("Services:");
		if(body["services"].Size()==0)
			std::cout << " (none)" << std::endl;
		else{
			std::cout << '\n' << formatOutput(body["services"], body,
			                                     {{"Name","/name"},
			                                      {"Cluster IP","/clusterIP"},
			                                      {"External IP","/externalIP"},
			                                      {"Ports","/ports"}});
		}
		
		if(body.HasMember("details") && body["details"].HasMember("pods")){
			std::cout << '\n' << bold("Pods:") << '\n';
			for(const auto& pod : body["details"]["pods"].GetArray()){
				if(pod.HasMember("name"))
					std::cout << "  " << pod["name"].GetString() << '\n';
				else
					std::cout << "  " << "<unnamed>" << '\n';
				if(pod.HasMember("status"))
					std::cout << "    Status: " << pod["status"].GetString() << '\n';
				if(pod.HasMember("created"))
					std::cout << "    Created: " << pod["created"].GetString() << '\n';
				if(pod.HasMember("hostName"))
					std::cout << "    Host: " << pod["hostName"].GetString() << '\n';
				if(pod.HasMember("hostIP"))
					std::cout << "    Host IP: " << pod["hostIP"].GetString() << '\n';
				if(pod.HasMember("conditions")){
					std::cout << "    Conditions: ";
					bool firstCondition=true;
					//TODO: it would be nice to sort these in time order
					for(const auto& condition : pod["conditions"].GetArray()){
						if(firstCondition)
							firstCondition=false;
						else
							std::cout << "                ";
						//string comparison necessary because of stupid
						if(std::string(condition["status"].GetString())=="True"){
							if(condition.HasMember("type"))
								std::cout << condition["type"].GetString();
							if(condition.HasMember("lastTransitionTime") && condition["lastTransitionTime"].IsString())
								std::cout << " at " << condition["lastTransitionTime"].GetString();
						}
						else{
							if(condition.HasMember("type"))
								std::cout << condition["type"].GetString();
							if(condition.HasMember("reason"))
								std::cout << ": " << condition["reason"].GetString();
							if(condition.HasMember("message"))
								std::cout << "; " << condition["message"].GetString();
						}
						std::cout << '\n';
					}
				}
				if(pod.HasMember("containers")){
					std::cout << "    " << "Containers:" << '\n';
					for(const auto& container : pod["containers"].GetArray()){
						if(container.HasMember("name"))
							std::cout << "      " << container["name"].GetString() << '\n';
						else
							std::cout << "      " << "<unnamed>" << '\n';
						if(container.HasMember("state") && !container["state"].ObjectEmpty()){
							std::cout << "        State: ";
							bool firstState=true;
							for(const auto& state : container["state"].GetObject()){
								if(firstState)
									firstState=false;
								else
									std::cout << "               ";
								std::cout << state.name.GetString();
								if(state.value.HasMember("startedAt"))
									std::cout << " since " << state.value["startedAt"].GetString();
								std::cout << '\n';
							}
						}
						if(container.HasMember("ready"))
							std::cout << "        Ready: " << (container["ready"].GetBool()?"true":"false") << '\n';
						if(container.HasMember("restartCount"))
							std::cout << "        Restarts: " << container["restartCount"].GetUint() << '\n';
						if(container.HasMember("image"))
							std::cout << "        Image: " << container["image"].GetString() << '\n';
					}
				}
			}
		}
		
		std::cout << '\n' << bold("Configuration:");
		if(body["metadata"]["configuration"].IsNull()
		   || (body["metadata"]["configuration"].IsString() && 
		       std::string(body["metadata"]["configuration"].GetString())
		       .find_first_not_of(" \t\n\r\v") == std::string::npos))
			std::cout << " (default)" << std::endl;
		else
			std::cout << "\n" << body["metadata"]["configuration"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to get application instance info";
		showError(response.body);
	}
}

void Client::restartInstance(const InstanceOptions& opt){
	ProgressToken progress(pman_,"Restarting instance...");
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance restart command requires an instance ID, not a name");
	
	auto url=makeURL("instances/"+opt.instanceID+"/restart");
	auto response=httpRequests::httpPut(url,"",defaultOptions());
	if(response.status==200)
		std::cout << "Successfully restarted instance " << opt.instanceID << std::endl;
	else{
		std::cerr << "Failed to restart instance " << opt.instanceID;
		showError(response.body);
	}
}

void Client::deleteInstance(const InstanceDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting instance...");
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance delete command requires an instance ID, not a name");
	
	auto url=makeURL("instances/"+opt.instanceID);
	if(opt.force)
		url+="&force";
	auto response=httpRequests::httpDelete(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted instance " << opt.instanceID << std::endl;
	else{
		std::cerr << "Failed to delete instance " << opt.instanceID;
		showError(response.body);
	}
}

void Client::fetchInstanceLogs(const InstanceLogOptions& opt){
	ProgressToken progress(pman_,"Fetching instance logs...");
	if(!verifyInstanceID(opt.instanceID))
		throw std::runtime_error("The instance logs command requires an instance ID, not a name");
	
	std::string url=makeURL("instances/"+opt.instanceID+"/logs");
	if(opt.maxLines)
		url+="&max_lines="+std::to_string(opt.maxLines);
	if(!opt.container.empty())
		#warning TODO: container name should be URL encoded
		url+="&container="+opt.container;
	if(opt.previousLogs)
		url+="&previous";
	auto response=httpRequests::httpGet(url,defaultOptions());
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		auto ptr=rapidjson::Pointer("/logs").Get(body);
		if(ptr==NULL)
			throw std::runtime_error("Failed to extract log data from server response");
		if (clientShouldPrintOnlyJson()){
			std::cout << formatOutput(body, body, {{"Logs","/logs"}});
		}else{
			std::string logData=ptr->GetString();
			std::cout << logData;
			if(!logData.empty() && logData.back()!='\n') std::cout << '\n';
		}
	}
	else{
		std::cerr << "Failed to get application instance logs";
		showError(response.body);
	}
}

void Client::listSecrets(const SecretListOptions& opt){
	ProgressToken progress(pman_,"Fetching secret list...");
	std::string url=makeURL("secrets") + "&vo="+opt.vo;

	std::vector<columnSpec> columns = {{"Name","/metadata/name"},
					   {"Created","/metadata/created",true},
					   {"Cluster","/metadata/cluster"},
					   {"ID","/metadata/id",true}};
	
	if(!opt.cluster.empty()) {
		url+="&cluster="+opt.cluster;
		columns = {{"Name","/metadata/name"},
			   {"Created","/metadata/created",true},
			   {"ID","/metadata/id",true}};
	}
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             columns);
	}
	else{
		std::cerr << "Failed to list secrets";
		showError(response.body);
	}
}

void Client::getSecretInfo(const SecretOptions& opt){
	ProgressToken progress(pman_,"Fetching secret info...");
	if(!verifySecretID(opt.secretID))
		throw std::runtime_error("The secret info command requires a secret ID, not a name");
	
	std::string url=makeURL("secrets/"+opt.secretID);
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		std::cout << formatOutput(body, body,
		                             {{"Name","/metadata/name"},
		                              {"Created","/metadata/created",true},
		                              {"VO","/metadata/vo"},
		                              {"Cluster","/metadata/cluster"},
		                              {"ID","/metadata/id",true}});
		
		if(clientShouldPrintOnlyJson()){return;}
		
		std::cout << '\n' << bold("Contents:") << "\n";

		if(!body.HasMember("contents") || !body["contents"].IsObject()){
			std::cerr << "Malformed secret data; no valid contents" << std::endl;
			return;
		}
		std::vector<std::vector<std::string>> decodedData;
		if(outputFormat!="no-headers")
			decodedData.emplace_back(std::vector<std::string>{"Key","Value"});
		for(auto itr = body["contents"].MemberBegin(); itr != body["contents"].MemberEnd(); itr++){
			decodedData.emplace_back();
			auto& row=decodedData.back();
			auto key = itr->name.GetString();
			if(!key)
				throw std::runtime_error("Malformed secret data; non-string key");
			row.push_back(key);
			auto val = itr->value.GetString();
			if (!val)
				throw std::runtime_error("Malformed secret data; non-string value");
			row.push_back(decodeBase64(val));
		}
		std::cout << formatTable(decodedData, {{"Key","",false},{"Value","",true}}, outputFormat!="no-headers");
	}
	else{
		std::cerr << "Failed to get secret info";
		showError(response.body);
	}
}

void Client::createSecret(const SecretCreateOptions& opt){
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();

	ProgressToken progress(pman_,"Creating secret...");
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.name, alloc);
	metadata.AddMember("vo", opt.vo, alloc);
	metadata.AddMember("cluster", opt.cluster, alloc);
	request.AddMember("metadata", metadata, alloc);
	rapidjson::Value contents(rapidjson::kObjectType);
	int currItem = 1;
	for (auto item : opt.data) {
		if (item.find("=") != std::string::npos) {
			auto keystr = item.substr(0, item.find("="));
			if (keystr.empty()) {
				std::cout << "Failed to create secret: No key given with value " << item << std::endl;
				return;
			}
			auto val = item.substr(item.find("=") + 1);
			if (val.empty()) {
				std::cout << "Failed to create secret: No value given with key " << keystr << std::endl;
			        return;
			}
			
			rapidjson::Value key;
			key.SetString(keystr.c_str(), keystr.length(), alloc);
			std::string encodedValue=encodeBase64(val);
			contents.AddMember(key, encodedValue, alloc);
			pman_.SetProgress((float)currItem / (float)opt.data.size());
			currItem++;
		} else {
			std::cout << "Failed to create secret: The key, value pair " << item << " is not in the required form key=val" << std::endl;
			return;
		}
	}

	request.AddMember("contents", contents, alloc);
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(makeURL("secrets"),buffer.GetString(),defaultOptions());
	
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully created secret " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to create secret " << opt.name;
		showError(response.body);
	}
}

void Client::copySecret(const SecretCopyOptions& opt){
	if(!verifySecretID(opt.sourceID))
		throw std::runtime_error("The secret copy command requires a secret ID as the source, not a name");
	
	ProgressToken progress(pman_,"Copying secret...");
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha1", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.name, alloc);
	metadata.AddMember("vo", opt.vo, alloc);
	metadata.AddMember("cluster", opt.cluster, alloc);
	request.AddMember("metadata", metadata, alloc);
	request.AddMember("copyFrom", opt.sourceID, alloc);
	
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	auto response=httpRequests::httpPost(makeURL("secrets"),buffer.GetString(),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully created secret " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to create secret " << opt.name;
		showError(response.body);
	}
}

void Client::deleteSecret(const SecretDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting secret...");
	if(!verifySecretID(opt.secretID))
		throw std::runtime_error("The secret delete command requires a secret ID, not a name");
	
	auto url=makeURL("secrets/"+opt.secretID);
	if(opt.force)
		url+="&force";
	auto response=httpRequests::httpDelete(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted secret " << opt.secretID << std::endl;
	else{
		std::cerr << "Failed to delete secret " << opt.secretID;
		showError(response.body);
	}
}

std::string Client::getDefaultEndpointFilePath(){
	std::string path=getHomeDirectory();
	path+=".slate/endpoint";
	return path;
}

std::string Client::getDefaultCredFilePath(){
	std::string path=getHomeDirectory();
	path+=".slate/token";
	return path;
}

std::string Client::fetchStoredCredentials(){
	PermState perms=checkPermissions(credentialPath);
	if(perms==PermState::INVALID)
		throw std::runtime_error("Credentials file "+credentialPath+
		                         " has wrong permissions; should be 0600 and owned by the current user");
	std::string token;
	if(perms==PermState::DOES_NOT_EXIST)
		throw std::runtime_error("Credentials file "+credentialPath+" does not exist");
	
	std::ifstream credFile(credentialPath);
	if(!credFile) //this mostly shouldn't happen since we already checked the permissions
		throw std::runtime_error("Failed to open credentials file "+credentialPath+" for reading");
	
	credFile >> token;
	if(credFile.fail())
		throw std::runtime_error("Failed to read credentials file "+credentialPath+"");
	
	return token;
}

std::string Client::getToken(){
	if(token.empty()){ //need to read in
		if(credentialPath.empty()) //use default if not specified
			credentialPath=getDefaultCredFilePath();
		token=fetchStoredCredentials();
	}
	return token;
}

std::string Client::getEndpoint(){
	if(apiEndpoint.empty()){ //need to read in
		if(endpointPath.empty())
			endpointPath=getDefaultEndpointFilePath();
		PermState perms=checkPermissions(endpointPath);
		if(perms!=PermState::DOES_NOT_EXIST){
			//don't actually care about permissions, be we should only try to
			//read if the file exists
			std::ifstream endpointFile(endpointPath);
			if(!endpointFile) //this mostly shouldn't happen since we already checked the permissions
				throw std::runtime_error("Failed to open endpoint file "+endpointPath+" for reading");
			
			endpointFile >> apiEndpoint;
			if(endpointFile.fail())
				throw std::runtime_error("Failed to read endpoint file "+endpointPath+"");
		}
		else{ //use default value
			apiEndpoint="http://localhost:18080";
		}
	}
	auto schemeSepPos=apiEndpoint.find("://");
	//there should be a scheme separator
	if(schemeSepPos==std::string::npos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
	//there should be a scheme before the separator
	if(schemeSepPos==0)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like it has a valid URL scheme");
	//the scheme should contain only letters, digits, +, ., and -
	if(apiEndpoint.find_first_not_of("abcdefghijklmnopqrstuvwxzy0123456789+.-")<schemeSepPos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like it has a valid URL scheme");
	//have something after the scheme
	if(schemeSepPos+3>=apiEndpoint.size())
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
	//no query string is permitted
	if(apiEndpoint.find('?')!=std::string::npos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no query is permitted");
	//no fragment is permitted
	if(apiEndpoint.find('#')!=std::string::npos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no fragment is permitted");
	//try to figure out where the hostname starts
	auto hostPos=schemeSepPos+3;
	if(apiEndpoint.find('@',hostPos)!=std::string::npos)
		hostPos=apiEndpoint.find('@',hostPos)+1;
	//have a hostname
	if(hostPos>=apiEndpoint.size())
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
	auto portPos=apiEndpoint.find(':',hostPos);
	//no slashes are permitted before the port
	if(apiEndpoint.find('/',hostPos)<portPos)
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no path (including a trailing slash) is permitted");
	if(portPos!=std::string::npos){
		portPos++;
		if(portPos>=apiEndpoint.size())
			throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look like a valid URL");
		//after the start of the port, there may be only digits
		if(apiEndpoint.find_first_not_of("0123456789",portPos)!=std::string::npos)
			throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
			                         "port number may contain only digits and no path "
			                         "(including a trailing slash) is permitted");
	}
	if(apiEndpoint[apiEndpoint.size()-1]=='/')
		throw std::runtime_error("Endpoint '"+apiEndpoint+"' does not look valid; "
		                         "no path (including a trailing slash) is permitted");
	
	return apiEndpoint;
}

httpRequests::Options Client::defaultOptions(){
	httpRequests::Options opts;
#ifdef USE_CURLOPT_CAINFO
	detectCABundlePath();
	opts.caBundlePath=caBundlePath;
#endif
	return opts;
}

#ifdef USE_CURLOPT_CAINFO
void Client::detectCABundlePath(){
	if(caBundlePath.empty()){
		//collection of known paths, copied from curl's acinclude.m4
		const static auto possiblePaths={
			"/etc/ssl/certs/ca-certificates.crt",     //Debian systems
			"/etc/pki/tls/certs/ca-bundle.crt",       //Redhat and Mandriva
			"/usr/share/ssl/certs/ca-bundle.crt",     //old(er) Redhat
			"/usr/local/share/certs/ca-root-nss.crt", //FreeBSD
			"/etc/ssl/cert.pem",                      //OpenBSD, FreeBSD (symlink)
			"/etc/ssl/certs/",                        //SUSE
		};
		for(const auto path : possiblePaths){
			if(checkPermissions(path)!=PermState::DOES_NOT_EXIST){
				caBundlePath=path;
				return;
			}
		}
	}
}
#endif

namespace{
	static const std::string base64Chars=
	"0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"-_";
}

bool Client::verifyInstanceID(const std::string& id){
	if(id.size()!=20)
		return false;
	if(id.find("instance_")!=0)
		return false;
	if(id.find_first_not_of(base64Chars,9)!=std::string::npos)
		return false;
	return true;
}

bool Client::verifySecretID(const std::string& id){
	if(id.size()!=18)
		return false;
	if(id.find("secret_")!=0)
		return false;
	if(id.find_first_not_of(base64Chars,7)!=std::string::npos)
		return false;
	return true;
}

void Client::filterInstanceNames(rapidjson::Document& json, std::string pointer){
	auto filterName=[&json](rapidjson::Value& item){
		std::string VO=rapidjson::Pointer("/metadata/vo").Get(item)->GetString();
		VO+='-';
		rapidjson::Value* nameValue=rapidjson::Pointer("/metadata/name").Get(item);
		std::string name=nameValue->GetString();
		if(name.find(VO)==0)
			name.erase(0,VO.size());
		nameValue->SetString(name.c_str(),name.size(),json.GetAllocator());
	};
	rapidjson::Value* item=rapidjson::Pointer(pointer.c_str()).Get(json);
	if(item->IsArray()){
		for(auto& item_ : item->GetArray())
			filterName(item_);
	}
	else
		filterName(*item);
}
