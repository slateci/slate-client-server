#include "Utilities.h"

#include "Logging.h"

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
