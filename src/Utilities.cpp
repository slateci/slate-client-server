#include "Utilities.h"

#include <boost/date_time/posix_time/posix_time.hpp>

#include "Logging.h"

std::string timestamp(){
	auto now = boost::posix_time::second_clock::universal_time();
	return to_simple_string(now)+" UTC";
}

const User authenticateUser(PersistentStore& store, const char* token){
	if(token==nullptr) //no token => no way of identifying a valid user
		return User{};
	return store.findUserByToken(token);
}

crow::json::wvalue generateError(const std::string& message){
	crow::json::wvalue err;
	err["kind"]="Error";
	err["message"]=message;
	return err;
}

std::string runCommand(const std::string& command){
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE,int(*)(FILE*)> pipe(popen(command.c_str(), "r"), pclose);
    if(!pipe)
		log_fatal("popen() failed!");
    while(!feof(pipe.get())){
        if(fgets(buffer.data(), 128, pipe.get()) != nullptr)
            result += buffer.data();
    }
    return result;
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
	return output;
}

inline std::string trim(const std::string &s) {
    auto wsfront = std::find_if_not(s.begin(),s.end(),[](int c){return std::isspace(c);});
    auto wsback = std::find_if_not(s.rbegin(),s.rend(),[](int c){return std::isspace(c);}).base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

std::vector<std::string> string_split_lines(const std::string &text) {
    std::stringstream ss(text);
    std::vector<std::string> lines;
    std::string line;
    while(std::getline(ss, line)){
        lines.push_back(line);
    }

    return lines;
}

std::vector<std::string> string_split_columns(const std::string &line, const char &delim) {
    std::stringstream ss(line);
    std::vector<std::string> tokens;
    std::string item;
    while (std::getline(ss, item, delim)) {
        tokens.push_back(trim(item));
    }
    return tokens;
}
