#include "Utilities.h"

#include <boost/date_time/posix_time/posix_time.hpp>

#include "Logging.h"

std::string timestamp(){
	auto now = boost::posix_time::second_clock::universal_time();
	return to_simple_string(now)+" UTC";
}

std::string generateError(const std::string& message){
	rapidjson::Document err(rapidjson::kObjectType);
	err.AddMember("kind", "Error", err.GetAllocator());
	err.AddMember("message", rapidjson::StringRef(message.c_str()), err.GetAllocator());
	
	rapidjson::StringBuffer errBuffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(errBuffer);
	err.Accept(writer);
  
	return errBuffer.GetString();
}

std::string fixInvalidEscapes(const std::string& message){
	std::ostringstream stream;
	for (char c : message) {
		switch (c){
		case '\n':
			stream << "\\n";
			break;
		case '\\':
			stream << "\\\\";
			break;
		case '\t':
			stream << "\\t";
			break;
		case '\r':
			stream << "\\r";
			break;
		default:
			stream << c;
		}
	}
	return stream.str();
}

commandResult runCommand(const std::string& command){
	std::array<char, 128> buffer;
	std::string result;
	struct pcloser{
		///\param r an integer to which the exit status of the child process should be stored
		pcloser(int& r):result(r){ result=0; }
		void operator()(FILE* f){
			auto tmp=pclose(f); 
			result=WEXITSTATUS(tmp);
		}
		int& result;
	};
	int status;
	std::unique_ptr<FILE,pcloser> pipe(popen(command.c_str(), "r"), pcloser{status});
	if(!pipe)
		log_fatal("popen() failed!");
	while(!feof(pipe.get())){
		if(fgets(buffer.data(), 128, pipe.get()) != nullptr)
			result += buffer.data();
	}
	pipe.reset(); //ensure we have the exit status
	return commandResult{result,status};
}

bool fetchFromEnvironment(const std::string& name, std::string& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

std::string reduceYAML(const std::string& input){
	enum State{
		def, //default, not in pure whitespace or a comment
		whitespace, //a line which has so far only contained whitespace
		comment,
		newline //directly _after_ a newline character
	} state=def;
	std::size_t pos=0, last_handled=0, line=1;
	std::string output;
	for(char c : input){
		switch(state){
			case def:
				if(c=='#'){
					//std::cout << "Line " << line << ": start of comment" << std::endl;
					state=comment;
					if(pos>last_handled){
						//std::cout << " Copying preceding data, pos=" << last_handled 
						//          << " len=" << (pos-last_handled) << std::endl;
						output+=input.substr(last_handled,pos-last_handled)+'\n';
						last_handled=pos;
					}
				}
				if(c=='\n'){
					//std::cout << "Line " << line << ": end of line" << std::endl;
					//std::cout << " Copying preceding data, pos=" << last_handled 
					//          << " len=" << (pos-last_handled) << std::endl;
					//copy this line, in case next turns out to be elidable
					if(pos-last_handled) //if there was something there
						output+=input.substr(last_handled,pos-last_handled)+'\n';
					last_handled=pos;
					state=newline;
				}
				break;
			case whitespace:
				if(c=='\n'){
					//std::cout << "Line " << line << ": entire line was whitespace" << std::endl;
					//whole line was whitespace, mark it handled so it will not be copied
					last_handled=pos;
					state=newline;
				}
				else if(c=='#'){
					//std::cout << "Line " << line << ": transitioned from whitespace to comment" << std::endl;
					state=comment;
				}
				else if(!std::isspace(c)){
					//std::cout << "Line " << line << ": whitespace ended with " << (int)c << std::endl;
					state=def;
				}
				break;
			case comment:
				//comments only end at newlines
				if(c=='\n'){
					//std::cout << "Line " << line << ": comment ended" << std::endl;
					//mark everything in the last segment handled so it will not be copied
					last_handled=pos;
					state=newline;
				}
				break;
			case newline:
				line++;
				last_handled=pos; //never explicitly copy newlines
				if(c=='\n'){
					state=newline;
					//ignore empty line
					last_handled=pos;
					//std::cout << "Line " << line << ": is empty" << std::endl;
				}
				else if(std::isspace(c)){
					state=whitespace;
					//std::cout << "Line " << line << ": began with whitespace" << std::endl;
				}
				else if(c=='#'){
					state=comment;
					//std::cout << "Line " << line << ": began with comment" << std::endl;
				}
				else{
					state=def;
					//std::cout << "Line " << line << ": began with text" << std::endl;
				}
				break;
		}
		pos++;
	}
	if(state==def){
		//std::cout << "Line " << line << ": implicit end of line (end of data)" << std::endl;
		//std::cout << " Copying preceding data, pos=" << last_handled 
		//          << " len=" << (pos-last_handled) << std::endl;
		//copy this line, in case next turns out to be elidable
		output+=input.substr(last_handled,pos-last_handled);
		last_handled=pos;
		state=newline;
	}
	//remove any trailing newline character
	if(!output.empty() && output.back()=='\n')
		output.resize(output.size()-1);
	return output;
}

std::string trim(const std::string &s){
    auto wsfront = std::find_if_not(s.begin(),s.end(),[](int c){return std::isspace(c);});
    auto wsback = std::find_if_not(s.rbegin(),s.rend(),[](int c){return std::isspace(c);}).base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

std::vector<std::string> string_split_lines(const std::string& text) {
    std::stringstream ss(text);
    std::vector<std::string> lines;
    std::string line;
    while(std::getline(ss, line)){
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> string_split_columns(const std::string& line, char delim, bool keepEmpty) {
    std::stringstream ss(line);
    std::vector<std::string> tokens;
    std::string item;
    while (std::getline(ss, item, delim)) {
		auto token=trim(item);
		if(!token.empty() || keepEmpty)
			tokens.push_back(token);
    }
    return tokens;
}
