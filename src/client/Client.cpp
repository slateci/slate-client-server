#include "client/Client.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <vector>

#include <zlib.h>

#include "client_version.h"
#include "Archive.h"
#include "Utilities.h"
#include "Process.h"
#include "OSDetection.h"

namespace{
	
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

///Insert newlines and copies of indent to make orig fit in the given maximum 
///width. Does not indent the first line. Will do the wrong thing with 
///multi-byte characters. 
///TODO: Be smarter about picking where to break
///TODO: Deal with newlines in the original string
std::string wrapWithIndent(const std::string orig, const std::string& indent, const std::size_t maxWidth){
	const std::size_t indentWidth=indent.size();
	std::size_t pos=0, remaining=orig.size();
	std::string result;
	bool firstLine=true;
	while(remaining){
		//figure out how much we can fit on this line
		std::size_t chunkSize=0;
		if(firstLine)
			chunkSize=maxWidth;
		else
			chunkSize=maxWidth-indentWidth;
		if(chunkSize>remaining)
			chunkSize=remaining;
		
		if(!firstLine){
			result+='\n';
			result+=indent;
		}
		result+=orig.substr(pos,chunkSize);
		
		pos+=chunkSize;
		remaining-=chunkSize;
		firstLine=false;
	}
	return result;
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

ClusterComponentOptions::ClusterComponentOptions():systemNamespace(Client::defaultSystemNamespace){}

const std::string Client::defaultSystemNamespace="slate-system";

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
			this->cond_.wait_until(lock,nextTick,[this]{ return(this->stop_); });
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
		if (foundIt != columns.end())
			indexer = std::distance(columns.begin(), foundIt);
		else
			indexer=-1;
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
				if (!attribute){
					if(col.optional)
						row.push_back("");
					else
						throw std::runtime_error("Given attribute does not exist");
				}
				else
					row.push_back(jsonValueToString(*attribute));
			}
		}
	}
	else if(jdata.IsObject()){
		data.emplace_back();
		auto& row=data.back();
		for(const auto& col : columns) {
			auto attribute = rapidjson::Pointer(col.attribute.c_str()).Get(jdata);
			if (!attribute){
				if(col.optional)
					row.push_back("");
				else
					throw std::runtime_error("Given attribute does not exist");
			}
			else
				row.push_back(jsonValueToString(*attribute));
		}
	}

	int subsetIndex = headers ? 1 : 0;

	if(indexer>=0)
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
apiVersion("v1alpha3"),
useANSICodes(useANSICodes),
outputWidth(outputWidth),
pman_(),
clusterComponents{
	{"ingressController",ClusterComponent{"An ingress controller","v1",
	                     &Client::checkIngressController,
	                     &Client::installIngressController,
	                     &Client::removeIngressController,
	                     &Client::upgradeIngressController,
	                     &Client::ensureIngressController}},
	{"federationRBAC",ClusterComponent{"RBAC roles for SLATE federation","v1",
	                  &Client::checkFederationRBAC,
	                  &Client::installFederationRBAC,
	                  &Client::removeFederationRBAC,
	                  &Client::installFederationRBAC, //update same as install
	                  nullptr/*&Client::ensureRBAC*/}},
	{"prometheusMonitoring",ClusterComponent{
	                        "Central monitoring provided by the SLATE platform","v1",
	                        &Client::checkPrometheusMonitoring,
	                        &Client::installPrometheusMonitoring,
	                        &Client::removePrometheusMonitoring,
	                        &Client::upgradePrometheusMonitoring,
	                        nullptr}}
}
{
	if(isatty(STDOUT_FILENO)){
		if(!this->outputWidth){ //determine width to use automatically
			struct winsize ws;
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
			this->outputWidth=ws.ws_col;
		}
		pman_.verbose_=true;
		this->enableFixup = true;
	}
	else {
		this->useANSICodes=false;
		this->enableFixup=false;
	}
		
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
		auto response=httpRequests::httpGet(getEndpoint()+"/version",defaultOptions());
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
			throw OperationFailed();
		}
	}catch(std::exception& ex){
		std::cerr << "Failed to contact API server " << getEndpoint() << ": " << ex.what() << std::endl;
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
			                  apiVersion+"; it cannot work with this server.") << std::endl;
			std::cout << "Try 'slate version upgrade' or consult your package manager." << std::endl;
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

void Client::createGroup(const GroupCreateOptions& opt){
	ProgressToken progress(pman_,"Creating group...");
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
  
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", rapidjson::StringRef(opt.groupName.c_str()), alloc);
	metadata.AddMember("scienceField", rapidjson::StringRef(opt.scienceField.c_str()), alloc);
	request.AddMember("metadata", metadata, alloc);
  
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);
  
	auto response=httpRequests::httpPost(makeURL("groups"),buffer.GetString(),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Successfully created group " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to create group " << opt.groupName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::updateGroup(const GroupUpdateOptions& opt){
	if(opt.email.empty() && opt.phone.empty() && opt.scienceField.empty() && opt.description.empty()){
		std::cout << "No updates specified" << std::endl;
		return;
	}

	ProgressToken progress(pman_,"Updating group...");
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", rapidjson::StringRef(opt.groupName.c_str()), alloc);
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
	
	auto response=httpRequests::httpPut(makeURL("groups/"+opt.groupName),buffer.GetString(),defaultOptions());
	if(response.status==200)
		std::cout << "Successfully updated group " << opt.groupName << std::endl;
	else{
		std::cerr << "Failed to update group " << opt.groupName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::deleteGroup(const GroupDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting group...");
	
	if(!opt.assumeYes){ 
		//check that the user really wants to do the deletion
		auto url=makeURL("groups/"+opt.groupName);
		auto response=httpRequests::httpGet(url,defaultOptions());
		if(response.status!=200){
			std::cerr << "Failed to get group " << opt.groupName;
			showError(response.body);
			throw std::runtime_error("Group deletion aborted");
		}
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Are you sure you want to delete group "
		  << resultJSON["metadata"]["id"].GetString() << " (" 
		  << resultJSON["metadata"]["name"].GetString() << ")? y/[n]: ";
			std::cout.flush();
			HideProgress quiet(pman_);
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="y" && answer!="Y")
				throw std::runtime_error("Group deletion aborted");
	}
	
	auto response=httpRequests::httpDelete(makeURL("groups/"+opt.groupName),defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted group " << opt.groupName << std::endl;
	else{
		std::cerr << "Failed to delete group " << opt.groupName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::getGroupInfo(const GroupInfoOptions& opt){
	ProgressToken progress(pman_,"Fetching group info...");
	auto url = makeURL("groups/"+opt.groupName);
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
		std::cerr << "Failed to get information about group " << opt.groupName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listGroups(const GroupListOptions& opt){
	ProgressToken progress(pman_,"Fetching group list...");
	auto url = makeURL("groups");
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
		std::cerr << "Failed to list groups";
		showError(response.body);
		throw OperationFailed();
	}
}

rapidjson::Document Client::getClusterList(std::string group){
	std::string url=Client::makeURL("clusters");
	if(!group.empty())
		url+="&group="+group;
	ProgressToken progress(pman_,"Fetching cluster list...");
	auto response=httpRequests::httpGet(url,defaultOptions());
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		return json;
	}
	else{
		std::cerr << "Failed to list clusters";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listClustersAccessibleToGroup(const GroupListAllowedOptions& opt){
	rapidjson::Document clusterListJSON = getClusterList(opt.groupName);
	ProgressToken progress(pman_,"Fetching accessible clusters...");
	if(!clusterListJSON.HasMember("items") || !clusterListJSON["items"].IsArray()){
		std::cerr << "Cluster list response from API server does not have expected structure";
		throw OperationFailed();
	}
	const rapidjson::Value& clusters = clusterListJSON["items"];
	rapidjson::Document accessRequest(rapidjson::kObjectType);
	auto& requestAlloc=accessRequest.GetAllocator();
	std::map<std::string,std::string> urlsToClusters;
	for (const auto& cluster : clusters.GetArray()) {
		if(!cluster.HasMember("metadata") || !cluster["metadata"].IsObject()
		   || !cluster["metadata"].HasMember("name") || !cluster["metadata"]["name"].IsString())
			continue;
		const rapidjson::Value& name = cluster["metadata"]["name"];
		std::string requestURL="/"+apiVersion+"/clusters/"+name.GetString()+"/allowed_groups/"+opt.groupName+"?token="+getToken();
		rapidjson::Value request(rapidjson::kObjectType);
		request.AddMember("method","GET",requestAlloc);
		request.AddMember("body","",requestAlloc);
		accessRequest.AddMember(rapidjson::Value().SetString(requestURL,requestAlloc),request,requestAlloc);
		urlsToClusters.emplace(requestURL,name.GetString());
	}
	
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	accessRequest.Accept(writer);
	auto response=httpRequests::httpPost(makeURL("multiplex"),buffer.GetString(),
										defaultOptions());
	
	if(response.status!=200){
		std::cerr << "Failed to get cluster group access information";
		showError(response.body);
		throw OperationFailed();
	}
	
	rapidjson::Document json;
	json.Parse(response.body.c_str());
	
	if(!json.IsObject()){
		std::cerr << "Cluster access response from API server does not have expected structure";
		throw OperationFailed();
	}
	
	std::vector<std::string> clustersToPrint;
	rapidjson::Document array(rapidjson::kArrayType);
	auto& allocator = array.GetAllocator();
	for(const auto& result : json.GetObject()){
		if(!result.value.HasMember("status") || !result.value.HasMember("body")
		   || !result.value["status"].IsInt() || !result.value["body"].IsString())
			continue;
		if(result.value["status"].GetInt()!=200)
			continue;
		
		try{
			auto url=result.name.GetString();
			if(urlsToClusters.find(url)==urlsToClusters.end())
				continue;
			auto name=urlsToClusters[url];
			rapidjson::Document resultBody;
			resultBody.Parse(result.value["body"].GetString());
			auto& gid = resultBody["group"];
			if(resultBody["accessAllowed"].IsBool() && resultBody["accessAllowed"].GetBool()){
				array.PushBack(rapidjson::Value(rapidjson::kObjectType)
							   .AddMember("cluster",
										  rapidjson::Value()
										  .SetString(name,allocator),
										  allocator)
							   .AddMember("gid",
										  rapidjson::Value()
										  .CopyFrom(gid,allocator),
										  allocator),
							   allocator);
			}
		}catch(std::exception& ex){
			continue;
		}
	}
	std::cout << formatOutput(array, array, {{"Cluster", "/cluster"},{"ID", "/gid", true}});
}

void Client::createCluster(const ClusterCreateOptions& opt){
	ProgressToken progress(pman_,"Creating cluster...");
	
	//This is a lengthy operation, and we don't actually talk to the API server 
	//until the end. Check now that the user has some credentials (although we 
	//cannot assess validity) in order to fail early in the common case of the 
	//user forgetting to install a token
	(void)getToken();
	
	std::string configPath=getKubeconfigPath(opt.kubeconfig);
	
	//set up the system namespace and service account
	ensureNRPController(configPath, opt.assumeYes);
	ensureRBAC(configPath, opt.assumeYes);
	ClusterConfig config=extractClusterConfig(configPath,opt.assumeYes);
	
	if(!opt.noIngress){
		//set up the ingress controller
		bool hasLoadBalancer=opt.assumeLoadBalancer;
		if(!hasLoadBalancer)
			hasLoadBalancer=checkLoadBalancer(configPath, opt.assumeYes);
		if(!hasLoadBalancer)
			throw std::runtime_error("SLATE's ingress controller needs a load balancer in order to function correctly.");
	
		try{
			ensureIngressController(configPath, config.namespaceName, opt.assumeYes);
		}catch(InstallAborted& ab){
			throw InstallAborted("Cluster registration aborted");
		}
		//check that the ingress controller gets allocated an address
		auto addr=getIngressControllerAddress(configPath,config.namespaceName);
		std::cout << " Ingress controller address: " << addr << std::endl;
	}
	
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.clusterName, alloc);
	metadata.AddMember("group", opt.groupName, alloc);
	metadata.AddMember("owningOrganization", opt.orgName, alloc);
	metadata.AddMember("kubeconfig", config.serviceAccountCredentials, alloc);
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
		if(resultJSON.HasMember("message") 
		  && resultJSON["message"].IsString()
		  && resultJSON["message"].GetStringLength()>0)
			std::cout << resultJSON["message"].GetString() << std::endl;
	}
	else{
		std::cerr << "Failed to create cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
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
	
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	if(!opt.orgName.empty())
		metadata.AddMember("owningOrganization", opt.orgName, alloc);
	if(opt.reconfigure || !opt.kubeconfig.empty()){
		std::string configPath=getKubeconfigPath(opt.kubeconfig);
		ClusterConfig config=extractClusterConfig(configPath,opt.assumeYes);
		metadata.AddMember("kubeconfig", config.serviceAccountCredentials, alloc);
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
		throw OperationFailed();
	}
}

void Client::deleteCluster(const ClusterDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting cluster...");
	
	if(!opt.assumeYes){ 
		//check that the user really wants to do the deletion
		auto url=makeURL("clusters/"+opt.clusterName);
		auto response=httpRequests::httpGet(url,defaultOptions());
		if(response.status!=200){
			std::cerr << "Failed to get cluster " << opt.clusterName;
			showError(response.body);
			throw std::runtime_error("Cluster deletion aborted");
		}
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Are you sure you want to delete cluster "
		  << resultJSON["metadata"]["id"].GetString() << " (" 
		  << resultJSON["metadata"]["name"].GetString() << ") belonging to group " 
		  << resultJSON["metadata"]["owningGroup"].GetString() << "? y/[n]: ";
			std::cout.flush();
			HideProgress quiet(pman_);
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="y" && answer!="Y")
				throw std::runtime_error("Cluster deletion aborted");
	}
	
	auto url=makeURL("clusters/"+opt.clusterName);
	if(opt.force)
		url+="&force";
	auto response=httpRequests::httpDelete(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted cluster " << opt.clusterName << std::endl;
	else{
		std::cerr << "Failed to delete cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listClusters(const ClusterListOptions& opt){
	rapidjson::Document json = getClusterList(opt.group);
	std::cout << formatOutput(json["items"], json,
			{{"Name","/metadata/name"},
			{"Admin","/metadata/owningGroup"},
			{"ID","/metadata/id",true}});
}

void Client::getClusterInfo(const ClusterInfoOptions& opt){
	auto url = makeURL("clusters/"+opt.clusterName);
	if (opt.all_nodes) url = url + "&nodes";
	ProgressToken progress(pman_,"Fetching cluster info...");
	auto response=httpRequests::httpGet(url,defaultOptions());
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json, json, 
		                          {{"Name", "/metadata/name"},
		                           {"Admin","/metadata/owningGroup"},
		                           {"Owner","/metadata/owningOrganization"},
								   {"Server Address", "/metadata/masterAddress", false, true},
		                           {"ID", "/metadata/id", true}
		                          });
		if(clientShouldPrintOnlyJson()){return;}
		if(json["metadata"].HasMember("location") && json["metadata"]["location"].IsArray()
		  && json["metadata"]["location"].GetArray().Size()>0){
			std::cout << '\n' << formatOutput(json["metadata"]["location"],
			                          json["metadata"]["location"],
			                          {{"Latitude","/lat"},{"Longitude","/lon"},{"Description","/desc",true,true}});
		}
		if(json["metadata"].HasMember("storageClasses") && json["metadata"]["storageClasses"].IsArray()
		  && json["metadata"]["storageClasses"].GetArray().Size()>0){
			std::cout << "\nStorage Classes:\n" 
			          << formatOutput(json["metadata"]["storageClasses"],
			                          json["metadata"]["storageClasses"],
			                          {{"Name","/name"},{"Default","/isDefault"},
			                           {"Expandable","/allowVolumeExpansion"},
			                           {"Binding","/bindingMode"},
			                           {"Reclaim Policy","/reclaimPolicy"}});
		}
		if(json["metadata"].HasMember("priorityClasses") && json["metadata"]["priorityClasses"].IsArray()
		  && json["metadata"]["priorityClasses"].GetArray().Size()>0){
			std::cout << "\nPriority Classes:\n" 
			          << formatOutput(json["metadata"]["priorityClasses"],
			                          json["metadata"]["priorityClasses"],
			                          {{"Name","/name"},{"Default","/isDefault"},
			                           {"Priority","/priority"},
			                           {"Description","/description",true,true}});
		}
		if(json["metadata"].HasMember("nodes") && json["metadata"]["nodes"].IsArray()
		  && json["metadata"]["nodes"].GetArray().Size()>0){
			std::cout << "\nNode Address Information:" << std::endl;
			//restructure data for more convenient printing in a table
			rapidjson::Document nodeData(rapidjson::kArrayType);
			rapidjson::Document::AllocatorType& alloc = nodeData.GetAllocator();
			for(const auto& node : json["metadata"]["nodes"].GetArray()) {
				bool first=true;
				if(!node["addresses"].IsArray())
					continue;
				for(const auto& addr : node["addresses"].GetArray()){
					rapidjson::Value entry(rapidjson::kObjectType);
					entry.AddMember("name",rapidjson::StringRef(first?node["name"].GetString():""),alloc);
					first=false;
					entry.AddMember("address",rapidjson::StringRef(addr["address"].GetString()),alloc);
					entry.AddMember("addressType",rapidjson::StringRef(addr["addressType"].GetString()),alloc);
					nodeData.PushBack(entry, alloc);
				}
			}
			std::string oldOrder=orderBy;
			orderBy="nothing";
			std::cout << formatOutput(nodeData, json["metadata"]["nodes"], 
			                          {{"Node", "/name"},
			                           {"Address", "/address"},
			                           {"Type", "/addressType"}
									  }) << std::endl;
			orderBy=oldOrder;
		}
	}
	else{
		std::cerr << "Failed to get information about cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::grantGroupClusterAccess(const GroupClusterAccessOptions& opt){
	ProgressToken progress(pman_,"Granting group cluster access...");
	auto response=httpRequests::httpPut(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_groups/"
	                                            +opt.groupName),"",defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully granted group " << opt.groupName 
		          << " access to cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to grant group " << opt.groupName << " access to cluster " 
		          << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::revokeGroupClusterAccess(const GroupClusterAccessOptions& opt){
	ProgressToken progress(pman_,"Removing group cluster access...");
	auto response=httpRequests::httpDelete(makeURL("clusters/"
	                                               +opt.clusterName
	                                               +"/allowed_groups/"
	                                               +opt.groupName),
	                                       defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully revoked group " << opt.groupName 
		          << " access to cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to revoke group " << opt.groupName << " access to cluster " 
		          << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listGroupWithAccessToCluster(const ClusterAccessListOptions& opt){
	ProgressToken progress(pman_,"Fetching groups with cluster access...");
	auto response=httpRequests::httpGet(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_groups"),
	                                    defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", "/metadata/name"},
		      {"ID", "/metadata/id", true}});
	}
	else{
		std::cerr << "Failed to retrieve groups with access to cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listAllowedApplications(const GroupClusterAppUseListOptions& opt){
	ProgressToken progress(pman_,"Fetching allowed application list...");
	auto response=httpRequests::httpGet(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_groups/"
	                                            +opt.groupName
	                                            +"/applications"),
	                                    defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json, {{"Name", ""}});
	}
	else{
		std::cerr << "Failed to retrieve groups with access to cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::allowGroupUseOfApplication(const GroupClusterAppUseOptions& opt){
	ProgressToken progress(pman_,"Giving group access to use application...");
	auto response=httpRequests::httpPut(makeURL("clusters/"
	                                            +opt.clusterName
	                                            +"/allowed_groups/"
	                                            +opt.groupName
	                                            +"/applications/"
	                                            +opt.appName),"",defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully granted group " << opt.groupName 
		          << " permission to use " << opt.appName 
		          << " on cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to grant group " << opt.groupName << " permission to use " 
		          << opt.appName << " on cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::denyGroupUseOfApplication(const GroupClusterAppUseOptions& opt){
	ProgressToken progress(pman_,"Removing group access to use application...");
	auto response=httpRequests::httpDelete(makeURL("clusters/"
	                                               +opt.clusterName
	                                               +"/allowed_groups/"
	                                               +opt.groupName
	                                               +"/applications/"
	                                               +opt.appName),defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		std::cout << "Successfully removed group " << opt.groupName 
		          << " permission to use " << opt.appName 
		          << " on cluster " << opt.clusterName << std::endl;
	}
	else{
		std::cerr << "Failed to remove group " << opt.groupName << " permission to use " 
		          << opt.appName << " on cluster " << opt.clusterName;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::pingCluster(const ClusterPingOptions& opt){
	ProgressToken progress(pman_,"Testing cluster connectivity...");
	auto response=httpRequests::httpGet(makeURL("clusters/"+opt.clusterName+"/ping"),defaultOptions());
	if(this->clientShouldPrintOnlyJson())
		std::cout << response.body << std::endl;
	else{
		if(response.status==200){
			rapidjson::Document json;
			json.Parse(response.body.c_str());
			if(!json.HasMember("reachable") || !json["reachable"].IsBool())
				std::cout << "Got invalid response: " << response.body << std::endl;
				std::cout << "Cluster " << opt.clusterName 
				  << (json["reachable"].GetBool()?" is":" is not") 
				  << " reachable" << std::endl;
		}
		else{
			std::cerr << "Failed check cluster connectivity";
			showError(response.body);
			throw OperationFailed();
		}
	}
}

void Client::listClusterComponents() const{
	rapidjson::Document data(rapidjson::kArrayType);
	rapidjson::Document::AllocatorType& alloc=data.GetAllocator();
	for(const auto& component : clusterComponents){
		//std::cout << component.first << ": " << component.second.description << '\n';
		rapidjson::Value componentData(rapidjson::kObjectType);
		componentData.AddMember("name", component.first, alloc);
		componentData.AddMember("description", component.second.description, alloc);
		
		data.PushBack(componentData, alloc);
	}
	if(outputFormat.empty())
		std::cout << "Available components:\n";
	std::cout << formatOutput(data, data,
		                      {{"Name","/name"},
		                       {"Description","/description",true}});
}

namespace{
void checkSystemNamespace(const std::string& configPath, const std::string& systemNamespace){
	auto result=runCommand("kubectl",{"get","cluster",systemNamespace,
	                                  "--kubeconfig",configPath});
	if(result.status!=0)
		throw std::runtime_error("'"+systemNamespace+"' does not appear to be a SLATE system namespace");
}
}

void Client::listInstalledClusterComponents(const ClusterComponentListOptions& opt) const{
	std::string configPath=getKubeconfigPath(opt.kubeconfig);
	checkSystemNamespace(configPath,opt.systemNamespace);
	
	rapidjson::Document data(rapidjson::kArrayType);
	rapidjson::Document::AllocatorType& alloc=data.GetAllocator();
	
	for(const auto& component : clusterComponents){
		auto result=(this->*component.second.check)(configPath,opt.systemNamespace);
		rapidjson::Value componentData(rapidjson::kObjectType);
		componentData.AddMember("name", component.first, alloc);
		switch(result){
			case ClusterComponent::NotInstalled:
				if(opt.verbose)
					componentData.AddMember("status", "not installed", alloc);
				break;
			case ClusterComponent::OutOfDate:
				componentData.AddMember("status", "installed, out of date", alloc);
				break;
			case ClusterComponent::UpToDate:
				componentData.AddMember("status", "installed, up to date", alloc);
				break;
		}
		data.PushBack(componentData, alloc);
	}
	
	if(outputFormat.empty())
		std::cout << "Installed components:\n";
	std::cout << formatOutput(data, data,
		                      {{"Name","/name"},
		                       {"Status","/status"}});
}

void Client::checkClusterComponent(const ClusterComponentOptions& opt) const{
	auto compIt=clusterComponents.find(opt.componentName);
	if(compIt==clusterComponents.end())
		throw std::runtime_error("Unrecognized component name: "+opt.componentName);
	const auto& component=compIt->second;
	
	std::string configPath=getKubeconfigPath(opt.kubeconfig);
	checkSystemNamespace(configPath,opt.systemNamespace);
	auto result=(this->*component.check)(configPath,opt.systemNamespace);
	switch(result){
		case ClusterComponent::NotInstalled:
			std::cout << "The " << opt.componentName << " component is not installed" << std::endl;
			break;
		case ClusterComponent::OutOfDate:
			std::cout << "The " << opt.componentName << " component is installed but out of date" << std::endl;
			break;
		case ClusterComponent::UpToDate:
			std::cout << "The " << opt.componentName << " component is installed and up to date" << std::endl;
			break;
	}
}

void Client::addClusterComponent(const ClusterComponentOptions& opt) const{
	auto compIt=clusterComponents.find(opt.componentName);
	if(compIt==clusterComponents.end())
		throw std::runtime_error("Unrecognized component name: "+opt.componentName);
	const auto& component=compIt->second;
	
	std::string configPath=getKubeconfigPath(opt.kubeconfig);
	checkSystemNamespace(configPath,opt.systemNamespace);
	(this->*component.install)(configPath,opt.systemNamespace);
	std::cout << "Added " << opt.componentName << std::endl;
}

void Client::removeClusterComponent(const ClusterComponentOptions& opt) const{
	auto compIt=clusterComponents.find(opt.componentName);
	if(compIt==clusterComponents.end())
		throw std::runtime_error("Unrecognized component name: "+opt.componentName);
	const auto& component=compIt->second;
	
	std::string configPath=getKubeconfigPath(opt.kubeconfig);
	checkSystemNamespace(configPath,opt.systemNamespace);
	(this->*component.remove)(configPath,opt.systemNamespace);
	std::cout << "Removed " << opt.componentName << std::endl;
}

void Client::upgradeClusterComponent(const ClusterComponentOptions& opt) const{
	auto compIt=clusterComponents.find(opt.componentName);
	if(compIt==clusterComponents.end())
		throw std::runtime_error("Unrecognized component name: "+opt.componentName);
	const auto& component=compIt->second;
	
	std::string configPath=getKubeconfigPath(opt.kubeconfig);
	checkSystemNamespace(configPath,opt.systemNamespace);
	(this->*component.upgrade)(configPath,opt.systemNamespace);
	std::cout << "Upgraded " << opt.componentName << std::endl;
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
		throw OperationFailed();
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
		throw OperationFailed();
	}
}
	
void Client::getApplicationDocs(const ApplicationConfOptions& opt){
	ProgressToken progress(pman_,"Fetching application documentation...");
	std::string url=makeURL("apps/"+opt.appName+"/info");
	if(opt.devRepo)
		url+="&dev";
	if(opt.testRepo)
		url+="&test";

	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200){
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::string info=resultJSON["spec"]["body"].GetString();
		//if the user specified a file, write there
		if(!opt.outputFile.empty()){
			std::ofstream confFile(opt.outputFile);
			if(!confFile)
				throw std::runtime_error("Unable to write documentation to "+opt.outputFile);
			confFile << info;
		}
		else //otherwise print to stdout
			std::cout << info << std::endl;
	}
	else{
		std::cerr << "Failed to get documentation for application " << opt.appName;
		showError(response.body);
		throw OperationFailed();
	}
}
	
void Client::installApplication(const ApplicationInstallOptions& opt){
	ProgressToken progress(pman_,"Installing application...");
	
	//figure out whether we are trying to directly install a chart
	bool directChart=false;
	{
		
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
	
	request.AddMember("apiVersion", "v1alpha3", alloc);
	request.AddMember("group", rapidjson::StringRef(opt.group.c_str()), alloc);
	request.AddMember("cluster", rapidjson::StringRef(opt.cluster.c_str()), alloc);
	request.AddMember("configuration", rapidjson::StringRef(configuration.c_str()), alloc);
	if(opt.fromLocalChart){
		struct stat data;
		int err=stat(opt.appName.c_str(),&data);
		if(err!=0){
			err=errno;
			throw std::runtime_error("Unable to stat "+opt.appName);
		}
		else{
			//TODO: verify that the file is a directory, has the structure 
			//of a helm chart, etc.
		}
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
	if(opt.fromLocalChart)
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
		throw OperationFailed();
	}
}

void Client::listInstances(const InstanceListOptions& opt){
	ProgressToken progress(pman_,"Fetching application instance list...");
	std::string url=makeURL("instances");

	std::vector<columnSpec> columns;
	if (opt.group.empty() && opt.cluster.empty())
		columns = {{"Name","/metadata/name"},
			   {"Group","/metadata/group"},
			   {"Cluster","/metadata/cluster"},
			   {"ID","/metadata/id",true}};
	
	if(!opt.group.empty()) {
		url+="&group="+opt.group;
		columns = {{"Name","/metadata/name"},
			   {"Cluster","/metadata/cluster"},
			   {"ID","/metadata/id",true}};
	}
	if(!opt.cluster.empty()) {
		url+="&cluster="+opt.cluster;
		columns = {{"Name","/metadata/name"},
			   {"Group","/metadata/group"},
			   {"ID","/metadata/id",true}};
	}
	if (!opt.cluster.empty() && !opt.group.empty())
		columns = {{"Name","/metadata/name"},
			   {"ID","/metadata/id",true}};
	
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document json;
		json.Parse(response.body.c_str());
		std::cout << formatOutput(json["items"], json,
		                             columns);
	}
	else{
		std::cerr << "Failed to list application instances";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::getInstanceInfo(const InstanceOptions& opt){
	ProgressToken progress(pman_,"Fetching instance information...");
	if(!verifyInstanceID(opt.instanceID)) {
		std::cerr << "The instance info command requires an instance ID, not a name" << std::endl;
		retryInstanceCommandWithFixup(&Client::getInstanceInfo, opt);
		return;
	}
	
	std::string url=makeURL("instances/"+opt.instanceID)+"&detailed";
	auto response=httpRequests::httpGet(url,defaultOptions());
	//TODO: handle errors, make output nice
	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		std::cout << formatOutput(body, body,
		                             {{"Name","/metadata/name"},
		                              {"Started","/metadata/created",true},
					      {"App Version", "/metadata/appVersion",false,true},
					      {"Chart Version", "/metadata/chartVersion",false,true},
		                              {"Group","/metadata/group"},
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
			                                      {"Ports","/ports"},
			                                      {"URL","/url"}});
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
				if(pod.HasMember("conditions") && pod["conditions"].IsArray() && pod["conditions"].Size()>0){
					std::multimap<std::string,std::string> conditions;
					for(const auto& condition : pod["conditions"].GetArray()){
						std::string key;
						std::ostringstream ss;
						if(std::string(condition["status"].GetString())=="True"){
							if(condition.HasMember("lastTransitionTime") && condition["lastTransitionTime"].IsString()){
								ss << '[' << condition["lastTransitionTime"].GetString() << "] ";
								key=condition["lastTransitionTime"].GetString();
							}
							if(condition.HasMember("type"))
								ss << condition["type"].GetString();
						}
						else{
							if(condition.HasMember("type"))
								ss << condition["type"].GetString();
							if(condition.HasMember("reason"))
								ss << ": " << condition["reason"].GetString();
							if(condition.HasMember("message"))
								ss << "; " << condition["message"].GetString();
						}
						conditions.emplace(key,ss.str());
					}
					bool firstCondition=true;
					const std::string indent="                ";
					for(const auto& condition : conditions){
						std::string str;
						if(firstCondition)
							str="    Conditions: ";
						else
							str=indent;
						str+=condition.second;
						str=wrapWithIndent(str,indent,outputWidth);
						std::cout << str << '\n';
						firstCondition=false;
					}
				}
				if(pod.HasMember("events") && pod["events"].IsArray() && pod["events"].Size()>0){
					std::multimap<std::string,std::string> events;
					for(const auto& event : pod["events"].GetArray()){
						std::string key;
						std::ostringstream ss;
						unsigned int count=1;
						if(event.HasMember("count"))
							count=event["count"].GetInt();
						if(count>1){
							if(event.HasMember("firstTimestamp") && event.HasMember("lastTimestamp")
							  && event["firstTimestamp"].IsString() && event["lastTimestamp"].IsString()){
								ss << '[' << event["firstTimestamp"].GetString() << " - " << event["lastTimestamp"].GetString() << "] ";
								key=event["firstTimestamp"].GetString();
							}
						}
						else{
							if(event.HasMember("firstTimestamp") && event["firstTimestamp"].IsString()){
								ss << '[' << event["firstTimestamp"].GetString() << "] ";
								key=event["firstTimestamp"].GetString();
							}
						}
						if(event.HasMember("reason") && event["reason"].IsString())
								ss << event["reason"].GetString() << ": ";
						if(event.HasMember("message") && event["message"].IsString())
							ss << event["message"].GetString();
						if(count>1)
							ss << " (x" << count << ')';
						events.emplace(key,ss.str());
					}
					bool firstEvent=true;
					const std::string indent="            ";
					for(const auto& event : events){
						std::string str;
						if(firstEvent)
							str="    Events: ";
						else
							str=indent;
						str+=event.second;
						str=wrapWithIndent(str,indent,outputWidth);
						std::cout << str << '\n';
						firstEvent=false;
					}
				}
				if(pod.HasMember("containers")){
					std::cout << "    " << "Containers:" << '\n';
					for(const auto& container : pod["containers"].GetArray()){
						if(container.HasMember("name") && container["name"].IsString())
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
								if(state.value.HasMember("startedAt") && state.value["startedAt"].IsString())
									std::cout << " since " << state.value["startedAt"].GetString();
								if(state.value.HasMember("exitCode") && state.value["exitCode"].IsInt())
									std::cout << " with status " << state.value["exitCode"].GetInt();
								std::cout << '\n';
							}
						}
						if(container.HasMember("ready"))
							std::cout << "        Ready: " << (container["ready"].GetBool()?"true":"false") << '\n';
						if(container.HasMember("restartCount"))
							std::cout << "        Restarts: " << container["restartCount"].GetUint() << '\n';
						if(container.HasMember("image"))
							std::cout << "        Image: " << container["image"].GetString() << '\n';
						if(container.HasMember("imageID"))
							std::cout << "        ImageID: " << container["imageID"].GetString() << '\n';
						if(container.HasMember("lastState") && !container["lastState"].ObjectEmpty()){
							std::cout << "        Last State: ";
							bool firstState=true;
							for(const auto& state : container["lastState"].GetObject()){
								if(firstState)
									firstState=false;
								else
									std::cout << "                    ";
								std::cout << state.name.GetString();
								if(state.value.HasMember("exitCode"))
									std::cout << " with status " << state.value["exitCode"].GetUint();
								if(state.value.HasMember("finishedAt"))
									std::cout << " at " << state.value["finishedAt"].GetString();
								if(state.value.HasMember("startedAt"))
									std::cout << "\n                      Started at " << state.value["startedAt"].GetString();
								if(state.value.HasMember("reason"))
									std::cout << "\n                      Reason: " << state.value["reason"].GetString();
								std::cout << '\n';
							}
						}
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
	} else if (response.status==404) {
		std::cerr << "Instance not found" << std::endl;
		retryInstanceCommandWithFixup(&Client::getInstanceInfo, opt);
		return;
	} else {
		std::cerr << "Failed to get application instance info";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::restartInstance(const InstanceOptions& opt){
	ProgressToken progress(pman_,"Restarting instance...");
	if(!verifyInstanceID(opt.instanceID)) {
		std::cerr << "The instance info command requires an instance ID, not a name" << std::endl;
		retryInstanceCommandWithFixup(&Client::restartInstance, opt);
		return;
	}
	
	auto url=makeURL("instances/"+opt.instanceID+"/restart");
	auto response=httpRequests::httpPut(url,"",defaultOptions());
	if(response.status==200){
		std::cout << "Successfully restarted instance " << opt.instanceID << std::endl;
		
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		if(resultJSON.IsObject()
		  && resultJSON.HasMember("message") 
		  && resultJSON["message"].IsString()
		  && resultJSON["message"].GetStringLength()>0)
			std::cout << resultJSON["message"].GetString() << std::endl;
	} else if (response.status==404) {
		std::cerr << "Instance not found" << std::endl;
		retryInstanceCommandWithFixup(&Client::restartInstance, opt);
		return;
	} else{
		std::cerr << "Failed to restart instance " << opt.instanceID;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::deleteInstance(const InstanceDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting instance...");
	if(!verifyInstanceID(opt.instanceID)) {
		std::cerr << "The instance info command requires an instance ID, not a name" << std::endl;
		retryInstanceCommandWithFixup(&Client::deleteInstance, opt);
		return;
	}
	
	if(!opt.assumeYes && !opt.force){ 
		//check that the user really wants to do the deletion
		auto url=makeURL("instances/"+opt.instanceID);
		auto response=httpRequests::httpGet(url,defaultOptions());
		if(response.status==404) {
			std::cerr << "Instance not found" << std::endl;
			retryInstanceCommandWithFixup(&Client::deleteInstance, opt);
			return;
		} else if(response.status!=200) {
			std::cerr << "Failed to get instance " << opt.instanceID;
			showError(response.body);
			throw std::runtime_error("Instance deletion aborted");
		}
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Are you sure you want to delete instance "
		  << resultJSON["metadata"]["id"].GetString() << " (" 
		  << resultJSON["metadata"]["name"].GetString() << ") belonging to group " 
		  << resultJSON["metadata"]["group"].GetString() << " from cluster " 
		  << resultJSON["metadata"]["cluster"].GetString() << "? y/[n]: ";
			std::cout.flush();
			HideProgress quiet(pman_);
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="y" && answer!="Y")
				throw std::runtime_error("Instance deletion aborted");
	}
	
	auto url=makeURL("instances/"+opt.instanceID);
	if(opt.force)
		url+="&force";
	auto response=httpRequests::httpDelete(url,defaultOptions());
	//TODO: other output formats
	if(response.status==200)
		std::cout << "Successfully deleted instance " << opt.instanceID << std::endl;
	else if(response.status==404) {
		std::cerr << "Instance not found" << std::endl;
		retryInstanceCommandWithFixup(&Client::deleteInstance, opt);
		return;
	} else {
		std::cerr << "Failed to delete instance " << opt.instanceID;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::fetchInstanceLogs(const InstanceLogOptions& opt){
	ProgressToken progress(pman_,"Fetching instance logs...");
	if(!verifyInstanceID(opt.instanceID)) {
		std::cerr << "The instance info command requires an instance ID, not a name" << std::endl;
		retryInstanceCommandWithFixup(&Client::fetchInstanceLogs, opt);
		return;
	}
	
	std::string url=makeURL("instances/"+opt.instanceID+"/logs");
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
	} else if (response.status==404) {
		std::cerr << "Instance not found" << std::endl;
		retryInstanceCommandWithFixup(&Client::fetchInstanceLogs, opt);
		return;
	} else {
		std::cerr << "Failed to get application instance logs";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::scaleInstance(const InstanceScaleOptions& opt){
	if(!verifyInstanceID(opt.instanceID)) {
		std::cerr << "The instance info command requires an instance ID, not a name" << std::endl;
		retryInstanceCommandWithFixup(&Client::scaleInstance, opt);
		return;
	}
	httpRequests::Response response;
	if(opt.instanceReplicas==InstanceScaleOptions::replicasNotSet){
		ProgressToken progress(pman_,"Checking instance scale...");
		
		std::string url=makeURL("instances/"+opt.instanceID+"/scale");
		if(!opt.deployment.empty())
			url+="&deployment="+opt.deployment;
		response=httpRequests::httpGet(url,defaultOptions());
		if (response.status==404) {
			std::cerr << "Instance not found" << std::endl;
			retryInstanceCommandWithFixup(&Client::scaleInstance, opt);
			return;
		}
		else if(response.status!=200){
			std::cerr << "Failed to get instance scale";
			showError(response.body);
			throw OperationFailed();
		}
	}
	else{
		ProgressToken progress(pman_,"Scaling instance...");
		
		std::string url=makeURL("instances/"+opt.instanceID+"/scale")+"&replicas="+std::to_string(opt.instanceReplicas);
		if(!opt.deployment.empty())
			url+="&deployment="+opt.deployment;
		response=httpRequests::httpPut(url,"",defaultOptions());
		if(response.status==200){
			if(!clientShouldPrintOnlyJson())
				std::cout << "Successfully scaled " << opt.instanceID << " to " 
				  << std::to_string(opt.instanceReplicas) << " replicas." << std::endl;
		} else if (response.status==404) {
			std::cerr << "Instance not found" << std::endl;
			retryInstanceCommandWithFixup(&Client::scaleInstance, opt);
			return;
		} else {
			std::cerr << "Failed to scale instance " << opt.instanceID;
			showError(response.body);
			throw OperationFailed();
		}
	}
	
	rapidjson::Document resultJSON;
	resultJSON.Parse(response.body.c_str());
	if(clientShouldPrintOnlyJson())
		formatOutput(resultJSON,resultJSON,{});
	else{
		std::vector<std::vector<std::string>> data;
		if(outputFormat!="no-headers")
			data.emplace_back(std::vector<std::string>{"Deployment","Replicas"});
		for(auto it=resultJSON["deployments"].MemberBegin(); it!=resultJSON["deployments"].MemberEnd(); it++){
			data.emplace_back();
			auto& row=data.back();
			auto key = it->name.GetString();
			if(!key)
				throw std::runtime_error("Malformed data; non-string key");
			row.push_back(key);
			if(!it->value.IsUint64())
				throw std::runtime_error("Malformed data; non-integer value");
			row.push_back(std::to_string(it->value.GetUint()));
		}
		std::cout << formatTable(data, {{"Deployment","",false},{"Replicas","",true}}, outputFormat!="no-headers");
	}
}

void Client::fetchCurrentProfile(){
	std::string url=makeURL("whoami");

	auto response=httpRequests::httpGet(url,defaultOptions());

	if(response.status==200){
		rapidjson::Document body;
		body.Parse(response.body.c_str());

		std::cout << formatOutput(body, body, {{"Name", "/metadata/name",true},
											   {"ID", "/metadata/id",true},
											   {"Email","/metadata/email",true},
											   {"Phone","/metadata/phone",true}});
		std::cout << formatOutput(body, body, {{"Institution", "/metadata/institution"},
											   {"Token", "/metadata/access_token",true},
											   {"Admin", "/metadata/admin"}});
		std::cout << formatOutput(body["metadata"]["groups"], body,
		                             {{"Groups", "", true}});
	} else {
		std::cerr << "Failed to fetch your profile ";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::getUserInfo(const UserOptions& opt){
	if(!verifyUserID(opt.id)) {
		std::cerr << "The user info command requires an user ID, not a name" << std::endl;
		return;
	}
	
	std::string url=makeURL("users/" + opt.id);

	auto response = httpRequests::httpGet(url,defaultOptions());
	if (response.status==200) {
		rapidjson::Document body;
		body.Parse(response.body.c_str());

		std::cout << formatOutput(body, body, {{"Name", "/metadata/name",true},
											   {"ID", "/metadata/id",true},
											   {"Email","/metadata/email",true},
											   {"Phone","/metadata/phone",true}});
		std::cout << formatOutput(body, body, {{"Institution", "/metadata/institution"},
											   {"Token", "/metadata/access_token",true},
											   {"Admin", "/metadata/admin"}});
		std::cout << formatOutput(body["metadata"]["groups"], body,
		                             {{"Groups", "", true}});
	} else {
		std::cerr << "Failed to fetch user info ";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listUsers(const UserOptions& opt){
	std::string url=makeURL("users");
	if(!opt.group.empty()) {
		if (!verifyGroupID(opt.group)) {
			std::cerr << "Group filter must be an ID, not a name" << std::endl;
			return;
		}
		url = url + "&group=" + opt.group;
	}
	
	auto response=httpRequests::httpGet(url,defaultOptions());
	
	if(response.status==200) {
		// Do not list phone number or email
		std::vector<columnSpec> columns = {{"Name", "/metadata/name"},
										   {"ID", "/metadata/id"},
										   {"Institution", "/metadata/institution"}};
		rapidjson::Document body;
		body.Parse(response.body.c_str());

		std::cout << formatOutput(body["items"], body, columns);
	} else {
		std::cerr << "Failed to list users ";
		if(!opt.group.empty()) std::cerr << "in group " << opt.group;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listUserGroups(const UserOptions& opt){
	if(!opt.id.empty() && !verifyUserID(opt.id)) {
		std::cerr << "The user groups command requires a user ID, not a name" << std::endl;
		return;
	}

	// If a user doesn't provide an ID we assume they mean to act on themselves
	std::string ID = opt.id;
	std::string name;
	std::string inst;
	rapidjson::Document usrInfo;
	auto response = (opt.id.empty())? httpRequests::httpGet(makeURL("whoami"),defaultOptions()) : httpRequests::httpGet(makeURL("users/" + opt.id),defaultOptions());
	usrInfo.Parse(response.body.c_str());
	if (response.status!= 200) {
		std::cerr << "Failed to list groups of user";
		showError(response.body);
		throw OperationFailed();
	}
	ID = std::string(usrInfo["metadata"]["id"].GetString());
	name = std::string(usrInfo["metadata"]["name"].GetString());
	inst = std::string(usrInfo["metadata"]["institution"].GetString());
	
	std::string url=makeURL("users/" + ID + "/groups");
	response=httpRequests::httpGet(url, defaultOptions());

	if (response.status==200) {
		rapidjson::Document body;
		body.Parse(response.body.c_str());
		std::cout << "Listing groups of " << ID << "(" << name << " - " << inst << ")" << std::endl;
		std::vector<columnSpec> columns = {{"Name", "/metadata/name"},
										   {"ID", "/metadata/id"}};

		std::cout << formatOutput(body["items"], body, columns);
	} else {
		std::cerr << "Failed to list groups of " << ID << "(" << name << " - " << inst << ")" << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
}

// Currently not supported (this function isn't registered and so shouldn't be called)
void Client::createUser(const CreateUserOptions &opt){
	std::string url=makeURL("users");
	std::cout << "Creating user..." << std::endl;
	
	// Prepare metadata
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();

	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("globusID", rapidjson::StringRef(opt.globID.c_str()), alloc);
	metadata.AddMember("name", rapidjson::StringRef(opt.name.c_str()), alloc);
	metadata.AddMember("email", rapidjson::StringRef(opt.email.c_str()), alloc);
	metadata.AddMember("phone", rapidjson::StringRef(opt.phone.c_str()), alloc);
	metadata.AddMember("institution", rapidjson::StringRef(opt.institution.c_str()), alloc);
	metadata.AddMember("admin", opt.admin, alloc);
	request.AddMember("metadata", metadata, alloc);

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);
	
	// Create User
	std::cout << "Sending user config to SLATE server..." << std::endl;
	auto response=httpRequests::httpPost(url, buffer.GetString(), defaultOptions());

	if(response.status==200) {
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
	  	std::cout << "Successfully created user " 
			  << resultJSON["metadata"]["name"].GetString()
			  << " with ID " << resultJSON["metadata"]["id"].GetString() << std::endl;
		if(resultJSON.HasMember("message") 
		  && resultJSON["message"].IsString()
		  && resultJSON["message"].GetStringLength()>0)
			std::cout << resultJSON["message"].GetString() << std::endl;
	} else {
		std::cerr << "Failed to create user";
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::removeUser(const UserOptions& opt){
	if(!verifyUserID(opt.id)) {
		std::cerr << "The delete user command requires an user ID, not a name" << std::endl;
		return;
	}
	// Fetch user info first!
	std::string url=makeURL("users/" + opt.id);
	auto response = httpRequests::httpGet(url,defaultOptions());
	if (response.status!=200) {
		std::cerr << "Failed to delete " << opt.id  << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
	rapidjson::Document usrInfo;
	usrInfo.Parse(response.body.c_str());

	// Verify user intent
	std::string usrName(usrInfo["metadata"]["name"].GetString());
	std::string usrInst(usrInfo["metadata"]["institution"].GetString());
	std::cout << "Are you sure you want to delete user " << usrName;
	if (!usrInst.empty()) std::cout << " (" << usrInst << ")";
	std::cout << "? [Y/n]: ";
	std::cout.flush();
	std::string answer;
	std::getline(std::cin,answer);
	if(answer!="y" && answer!="Y") return;
	
	// Perform deletion
	url=makeURL("users/" + opt.id);
	std::cout << "Deleting user..." << std::endl;
	response=httpRequests::httpDelete(url, defaultOptions());
	if(response.status==200) {
		std::cout << "Successfully deleted " << opt.id << " (" << usrName << " - " << usrInst << ")" << std::endl;
	} else {
		std::cerr << "Failed to delete " << opt.id << " (" << usrName << " - " << usrInst << ")" << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::updateUser(const UpdateUserOptions& opt){
	if(!opt.id.empty() && !verifyUserID(opt.id)) {
		std::cerr << "The user update command requires a user ID, not a name" << std::endl;
		return;
	}

	// If a user doesn't provide an ID we assume they mean to act on themselves
	std::string ID = opt.id;
	std::string name;
	std::string inst;
	rapidjson::Document usrInfo;
	auto response = (opt.id.empty())? httpRequests::httpGet(makeURL("whoami"),defaultOptions()) : httpRequests::httpGet(makeURL("users/" + opt.id),defaultOptions());
	usrInfo.Parse(response.body.c_str());
	if (response.status!= 200) {
		std::cerr << "Failed to update user";
		showError(response.body);
		throw OperationFailed();
	}
	ID = std::string(usrInfo["metadata"]["id"].GetString());
	name = std::string(usrInfo["metadata"]["name"].GetString());
	inst = std::string(usrInfo["metadata"]["institution"].GetString());

	std::string url=makeURL("users/" + ID);
	std::cout << "Updating user..." << std::endl;
	// Prepare metadata
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	if (!opt.name.empty()) metadata.AddMember("name", rapidjson::StringRef(opt.name.c_str()), alloc);
	if (!opt.email.empty()) metadata.AddMember("email", rapidjson::StringRef(opt.email.c_str()), alloc);
	if (!opt.phone.empty()) metadata.AddMember("phone", rapidjson::StringRef(opt.phone.c_str()), alloc);
	if (!opt.institution.empty()) metadata.AddMember("institution", rapidjson::StringRef(opt.institution.c_str()), alloc);
	request.AddMember("metadata", metadata, alloc);

	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	request.Accept(writer);

	std::cout << "Sending user config to SLATE server..." << std::endl;
	response=httpRequests::httpPut(url, buffer.GetString(), defaultOptions());

	if(response.status==200){
	  	std::cout << "Successfully updated " << ID << "(" << name << " - " << inst << ")" << std::endl;
	} else {
		std::cerr << "Failed to update user" << ID << "(" << name << " - " << inst << ")" << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::addUserToGroup(const UserOptions& opt){
	if(!verifyUserID(opt.id)) {
		std::cerr << "The add-to-group command requires a user ID, not a name" << std::endl;
		return;
	}
	if(!verifyGroupID(opt.group)) {
		std::cout << "Treating " << opt.group << " as a group name and not an ID..." << std::endl;
	}
	std::string url=makeURL("users/" + opt.id + "/groups/" + opt.group);
	auto response = httpRequests::httpPut(url, "");
	if(response.status==200){
		std::cout << "Successfully added " << opt.id << " to " << opt.group << std::endl;
	} else {
		std::cerr << "Failed to add " << opt.id << " to " << opt.group << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::removeUserFromGroup(const UserOptions& opt){
	if(!verifyUserID(opt.id)) {
		std::cerr << "The remove-from-group command requires a user ID, not a name" << std::endl;
		return;
	}
	if(!verifyGroupID(opt.group)) {
		std::cout << "Treating " << opt.group << " as a group name and not an ID..." << std::endl;
	}
	std::string url=makeURL("users/" + opt.id + "/groups/" + opt.group);
	auto response = httpRequests::httpDelete(url, defaultOptions());
	if(response.status==200){
		std::cout << "Successfully removed " << opt.id << " from " << opt.group << std::endl;
	} else {
		std::cerr << "Failed to remove " << opt.id << " from " << opt.group << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::updateUserToken(const UserOptions& opt){
	if(!opt.id.empty() && !verifyUserID(opt.id)) {
		std::cerr << "The replace token command requires a user ID, not a name" << std::endl;
		return;
	}
	std::string ID = opt.id;
	std::string usrName;
	std::string usrInst;
	// If a user doesn't provide an ID we assume they mean to act on themselves
	auto response = (opt.id.empty())? httpRequests::httpGet(makeURL("whoami"),defaultOptions()) : httpRequests::httpGet(makeURL("users/" + opt.id),defaultOptions());
	rapidjson::Document usrInfo;
	usrInfo.Parse(response.body.c_str());
	if (response.status!= 200) {
		std::cerr << "Failed to update user";
		showError(response.body);
		throw OperationFailed();
	}
	ID = std::string(usrInfo["metadata"]["id"].GetString());
	usrName = std::string(usrInfo["metadata"]["name"].GetString());
	usrInst = std::string(usrInfo["metadata"]["institution"].GetString());
	
	// Verify intent
	std::cout << "Are you sure you want to replace the token of " << ID << " (" << usrName;
	if (!usrInst.empty()) std::cout << " - " << usrInst;
	std::cout << ")? [Y/n]: ";
	std::cout.flush();
	std::string answer;
	std::getline(std::cin,answer);
	if(answer!="y" && answer!="Y") return;

	std::string url=makeURL("users/" + ID + "/replace_token");
	std::cout << "Sending request to SLATE server..." << std::endl;
	auto idcheck = httpRequests::httpGet(makeURL("whoami"), defaultOptions());
	response = httpRequests::httpGet(url, defaultOptions());
	if(response.status==200){
		rapidjson::Document tokbody;
		tokbody.Parse(response.body.c_str());
		std::cout << "Successfully renewed token of " << ID << " (" << usrName;
		if (!usrInst.empty()) std::cout << " - " << usrInst;
		std::cout << ") with new token " << tokbody["metadata"]["access_token"].GetString() << std::endl;
		if (idcheck.status==200) {
			rapidjson::Document body;
			body.Parse(idcheck.body.c_str());
			std::string reqid(body["metadata"]["id"].GetString());
			if (reqid == ID) {
				std::cout << "Token self-overwrite detected. Would you like to update your credentials file automatically? [Y/n]: ";
				std::cout.flush();
				std::getline(std::cin,answer);
				if(answer!= "" && answer!="y" && answer!="Y") return;
				std::string newtoken(tokbody["metadata"]["access_token"].GetString());
				updateStoredCredentials(newtoken);
				std::cout << "Successfully updated user credentials file" << std::endl;
			}
		} else {
			std::cerr << "Failed to check if user was overwriting their own token. Manual changes to user credentials may be necessary." << std::endl;
			showError(idcheck.body);
			return;
		}
	} else {
		// in case something weird happens
		std::cerr << "Failed to renew token of " << ID << " (" << usrName << " - "<< usrInst << ")" << std::endl;
		showError(response.body);
		throw OperationFailed();
	}
}

void Client::listSecrets(const SecretListOptions& opt){
	ProgressToken progress(pman_,"Fetching secret list...");
	std::string url=makeURL("secrets") + "&group="+opt.group;

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
		throw OperationFailed();
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
		                              {"Group","/metadata/group"},
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
		throw OperationFailed();
	}
}

void Client::createSecret(const SecretCreateOptions& opt){
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();

	ProgressToken progress(pman_,"Creating secret...");
	
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.name, alloc);
	metadata.AddMember("group", opt.group, alloc);
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
		throw OperationFailed();
	}
}

void Client::copySecret(const SecretCopyOptions& opt){
	if(!verifySecretID(opt.sourceID))
		throw std::runtime_error("The secret copy command requires a secret ID as the source, not a name");
	
	ProgressToken progress(pman_,"Copying secret...");
	rapidjson::Document request(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
	
	request.AddMember("apiVersion", "v1alpha3", alloc);
	rapidjson::Value metadata(rapidjson::kObjectType);
	metadata.AddMember("name", opt.name, alloc);
	metadata.AddMember("group", opt.group, alloc);
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
		throw OperationFailed();
	}
}

void Client::deleteSecret(const SecretDeleteOptions& opt){
	ProgressToken progress(pman_,"Deleting secret...");
	if(!verifySecretID(opt.secretID))
		throw std::runtime_error("The secret delete command requires a secret ID, not a name");
	
	if(!opt.assumeYes && !opt.force){ 
		//check that the user really wants to do the deletion
		auto url=makeURL("secrets/"+opt.secretID);
		auto response=httpRequests::httpGet(url,defaultOptions());
		if(response.status!=200){
			std::cerr << "Failed to get secret " << opt.secretID;
			showError(response.body);
			throw std::runtime_error("Secret deletion aborted");
		}
		rapidjson::Document resultJSON;
		resultJSON.Parse(response.body.c_str());
		std::cout << "Are you sure you want to delete secret "
		  << resultJSON["metadata"]["id"].GetString() << " (" 
		  << resultJSON["metadata"]["name"].GetString() << ") belonging to group " 
		  << resultJSON["metadata"]["group"].GetString() << " from cluster " 
		  << resultJSON["metadata"]["cluster"].GetString() << "? y/[n]: ";
			std::cout.flush();
			HideProgress quiet(pman_);
			std::string answer;
			std::getline(std::cin,answer);
			if(answer!="y" && answer!="Y")
				throw std::runtime_error("Secret deletion aborted");
	}
	
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
		throw OperationFailed();
	}
}

// Code is available under the Creative Commons Attribution-ShareAlike License
// Source: https://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C++
template<typename T>
typename T::size_type levensteinDistance(const T &source, const T &target){
    if (source.size() > target.size()) {
        return levensteinDistance(target, source);
    }

    using TSizeType = typename T::size_type;
    const TSizeType min_size = source.size(), max_size = target.size();
    std::vector<TSizeType> lev_dist(min_size + 1);

    for (TSizeType i = 0; i <= min_size; ++i) {
        lev_dist[i] = i;
    }

    for (TSizeType j = 1; j <= max_size; ++j) {
        TSizeType previous_diagonal = lev_dist[0], previous_diagonal_save;
        ++lev_dist[0];

        for (TSizeType i = 1; i <= min_size; ++i) {
            previous_diagonal_save = lev_dist[i];
            if (source[i - 1] == target[j - 1]) {
                	lev_dist[i] = previous_diagonal;
            } else {
                lev_dist[i] = std::min(std::min(lev_dist[i - 1], lev_dist[i]), previous_diagonal) + 1;
            }
            previous_diagonal = previous_diagonal_save;
        }
    }
    return lev_dist[min_size];
};

template<typename OptionsType>
void Client::retryInstanceCommandWithFixup(void (Client::* command)(const OptionsType&), OptionsType opt){
	if (!this->enableFixup) return;
	std::string badID=opt.instanceID;

	ProgressToken progress(pman_,"Fetching application instances...");
	std::string url=makeURL("instances");

	// Generally the group/cluster metadata won't be available anyways since they're mostly provided when listing instances, which won't invoke this function.
	std::vector<columnSpec> columns = {{"Name","/metadata/name"},
			   {"Group","/metadata/group"},
			   {"Cluster","/metadata/cluster"},
			   {"ID","/metadata/id",true}};

	auto response=httpRequests::httpGet(url,defaultOptions());

	if(response.status!=200){
		std::cerr << "Failed to fetch instances" << std::endl;
		return;
	} else {
		rapidjson::Document json;
		rapidjson::Document bestfits; // Stores the best candidates
		std::string::size_type bestdist = std::numeric_limits<std::size_t>::max();
		bestfits.SetArray();
		rapidjson::Document::AllocatorType & allocator = bestfits.GetAllocator();
		json.Parse(response.body.c_str());
		
		
		// Try to get a better ID/name
		for(rapidjson::SizeType i = 0; i < json["items"].Size(); i++){
			std::string candidate_id = json["items"][i]["metadata"]["id"].GetString();
			std::string candidate_name = json["items"][i]["metadata"]["name"].GetString();

			std::string::size_type s1 = levensteinDistance<std::string>(badID, candidate_id);
			std::string::size_type s2 = levensteinDistance<std::string>(badID, candidate_name);

			std::string::size_type betterdist = std::min(s1, s2); // Only the smallest of the two needs to be considered here
			if (betterdist < bestdist) {
				// Reset bestfits if an even better guess is found
				// Since each entry would have to be a worse fit
				bestfits.Clear();
				bestdist = betterdist;
				bestfits.PushBack(json["items"][i], allocator);
			} else if (betterdist == bestdist) {
				// Add to bestfits if it is just as good as our current best fit
				bestfits.PushBack(json["items"][i], allocator);
			}
		}

		// if the list is empty, just exit
		if(bestfits.Size() == 0) {
			std::cout << "No instances available for fixup" << std::endl;
			return;
		}
		// and if we only have one best fit, mention it specifically
		else if (bestfits.Size() == 1){
			std::string answer;
			{
				HideProgress quiet(pman_);
				std::cout << "Best guess for " << badID << " is " << bestfits[0]["metadata"]["id"].GetString() << " with name " << bestfits[0]["metadata"]["name"].GetString() << ". Use this instead? y/[n]: ";
				std::cout.flush();
				std::getline(std::cin,answer);
			}
			if(answer!="y" && answer!="Y") {
				std::cout << "Aborting fixup" << std::endl;
				return;
			}
			else {
				opt.instanceID=bestfits[0]["metadata"]["id"].GetString();
				(this->*command)(opt);
				return;
			}
		} else {
			std::string answer;
			{
				HideProgress quiet(pman_);
				std::cout << "Multiple guesses for " << badID << " found. List all " << bestfits.Size() << " of them? y/[n]: ";
				std::cout.flush();
				std::getline(std::cin,answer);
			}
			if(answer!="y" && answer!="Y") return;
			else {
				{
					HideProgress quiet(pman_);
					std::cout << formatOutput(bestfits, json, columns);
					std::cout << "To use one of these guesses, copy and paste its id. To abort, type \"n\" or \"quit\": ";
					std::cout.flush();
					std::getline(std::cin,answer);
				}
				if (answer != "" && answer != "n" && answer!= "N" && answer != "quit") {
					opt.instanceID=answer;
					(this->*command)(opt);
				}
				else {
					std::cout << "Aborting fixup" << std::endl;
				}
			}
		}
	}
}

std::string Client::getKubeconfigPath(std::string configPath) const{
	if(configPath.empty()) //try environment
		fetchFromEnvironment("KUBECONFIG",configPath);
	if(configPath.empty()) //try stardard default path
		configPath=getHomeDirectory()+".kube/config";
	if(checkPermissions(configPath)==PermState::DOES_NOT_EXIST)
		throw std::runtime_error("Config file '"+configPath+"' does not exist");
	return configPath;
}

std::string Client::getDefaultEndpointFilePath() const{
	std::string path=getHomeDirectory();
	path+=".slate/endpoint";
	return path;
}

std::string Client::getDefaultCredFilePath() const{
	std::string path=getHomeDirectory();
	path+=".slate/token";
	return path;
}

std::string Client::fetchStoredCredentials() const{
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

void Client::updateStoredCredentials(std::string token) {
	std::ofstream ofs(credentialPath, std::ofstream::trunc);
	ofs << token;
	ofs.close();
}

std::string Client::getToken() const{
	if(token.empty()){ //need to read in
		if(credentialPath.empty()) //use default if not specified
			credentialPath=getDefaultCredFilePath();
		token=fetchStoredCredentials();
	}
	return token;
}

std::string Client::getEndpoint() const{
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

httpRequests::Options Client::defaultOptions() const{
	httpRequests::Options opts;
#ifdef USE_CURLOPT_CAINFO
	detectCABundlePath();
	opts.caBundlePath=caBundlePath;
#endif
	return opts;
}

#ifdef USE_CURLOPT_CAINFO
void Client::detectCABundlePath() const{
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

bool Client::verifyUserID(const std::string& id){
	if(id.size()!=16)
		return false;
	if(id.find("user_")!=0)
		return false;
	if(id.find_first_not_of(base64Chars,5)!=std::string::npos)
		return false;
	return true;
}

bool Client::verifyGroupID(const std::string& id){
	if(id.size()!=17)
		return false;
	if(id.find("group_")!=0)
		return false;
	if(id.find_first_not_of(base64Chars,6)!=std::string::npos)
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
