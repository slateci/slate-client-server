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
