#include "Process.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <libcuckoo/cuckoohash_map.hh>

#include <Utilities.h>

void setNonblocking(int fd){
	int flags = fcntl(fd, F_GETFL);
	if(flags==-1){
		int err=errno;
		throw std::runtime_error("Unable to get fd flags: "+std::to_string(err));
	}
	flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if(flags==-1){
		int err=errno;
		throw std::runtime_error("Unable to set fd flags: "+std::to_string(err));
	}
}

namespace{
sig_atomic_t reapFlag=0;

void handleSIGCHLD(int, siginfo_t* info, void* uap){
	reapFlag=1;
	//const char msg[]="We get signal!\n";
	//write(1,msg,sizeof(msg));
}
	
struct PrepareForSignals{
	PrepareForSignals(){
		struct sigaction act;
		act.sa_flags=SA_RESTART | SA_NOCLDSTOP | SA_SIGINFO;
		act.sa_sigaction=handleSIGCHLD;
		int res=sigaction(SIGCHLD, &act, &oact);
		if(res==-1){
			auto err=errno;
			throw std::runtime_error("waitpid sigaction: "+std::to_string(err));
		}
	}
	~PrepareForSignals(){
		struct sigaction act;
		sigaction(SIGCHLD, &oact, &act);
		//if sigaction failed there's nothing useful to do about it
	}
	struct sigaction oact;
} signalPrep;
	
cuckoohash_map<pid_t,char> exitStatuses;
} //anonymous namespace

ProcessIOBuffer::ProcessIOBuffer():
fd_in(-1),fd_out(-1),
readBuffer(nullptr),
readEOF(true){
	setg(readBuffer,readBuffer,readBuffer);
}

ProcessIOBuffer::ProcessIOBuffer(int in, int out):
fd_in(in),fd_out(out),
readBuffer(new char_type[bufferSize]),
readEOF(false){
	if(in!=-1)
		setNonblocking(in);
	if(out!=-1)
		setNonblocking(out);
	setg(readBuffer,readBuffer,readBuffer);
}

ProcessIOBuffer::ProcessIOBuffer(ProcessIOBuffer&& other):
fd_in(other.fd_in),fd_out(other.fd_out),
readBuffer(other.readBuffer),
readEOF(other.readEOF){
	setg(readBuffer,readBuffer,readBuffer);
	other.fd_in=-1;
	other.fd_out=-1;
	other.readBuffer=nullptr;
}

ProcessIOBuffer::~ProcessIOBuffer(){
	if(fd_in!=-1)
		close(fd_in);
	if(fd_out!=-1)
		close(fd_out);
	delete[] readBuffer;
}

ProcessIOBuffer& ProcessIOBuffer::operator=(ProcessIOBuffer&& other){
	if(&other!=this){
		using std::swap;
		swap(fd_in,other.fd_in);
		swap(fd_out,other.fd_out);
		swap(readBuffer,other.readBuffer);
		swap(readEOF,other.readEOF);
		setg(readBuffer,readBuffer,readBuffer);
		other.setg(other.readBuffer,other.readBuffer,other.readBuffer);
	}
	return *this;
}

std::streamsize ProcessIOBuffer::xsputn(const char_type* s, std::streamsize n){
	std::streamsize amountWritten=0;
	while(n>0){
		waitReady(WRITE);
		int result=write(fd_in,s,n);
		if(result>0){
			n-=result;
			s+=result;
			amountWritten+=result;
			if(n<=0)
				return(amountWritten);
		}
		else{
			if(errno==EAGAIN || errno==EWOULDBLOCK)
				continue;
			//TODO: include error code, etc
			//throw std::runtime_error("Write Error");
			std::cerr << "write gave error " << errno << std::endl;
		}
	}
	return(amountWritten);
}

ProcessIOBuffer::traits_type::int_type ProcessIOBuffer::underflow(){
	if(readEOF)
		return(std::char_traits<char>::eof());
	if (gptr() < egptr()) // buffer not exhausted
		return(traits_type::to_int_type(*gptr()));
	std::streamsize amountRead=0;
	while(true){
		waitReady(READ);
		int result=read(fd_out,(void*)readBuffer,bufferSize);
		if(result<0){ //an error occurred
			int err=errno;
			if(err==EAGAIN)
				continue;
			std::cerr << "read gave result " << result;
			std::cerr << " and error " << err << std::endl;
			throw std::runtime_error("Read Error");
		}
		else if (result==0){
			readEOF=true;
			return(std::char_traits<char>::eof());
		}
		else
			amountRead+=result;
		break;
	}
	//update the stream buffer
	setg(readBuffer,readBuffer,readBuffer+amountRead);
	return(traits_type::to_int_type(*gptr()));
}

std::streamsize ProcessIOBuffer::showmanyc(){
	if(readEOF)
		return -1;
	if(waitReady(READ,false))
		return 1; //at least one character is available for reading now
	return 0; //impossible to tell whether more characters will be available
}

bool ProcessIOBuffer::waitReady(rw direction, bool wait){
	fd_set set;
	fd_set* readset=NULL;
	fd_set* writeset=NULL;
	FD_ZERO(&set);
	int maxfd=-1;
	if(direction==READ){
		FD_SET(fd_out,&set);
		maxfd=fd_out+1;
		readset=&set;
	}
	else{
		FD_SET(fd_in,&set);
		maxfd=fd_in+1;
		writeset=&set;
	}
	while(true){
		struct timeval timeout;
		timeout.tv_sec=0;
		timeout.tv_usec=0;
		int result=select(maxfd,readset,writeset,NULL,(wait?NULL:&timeout));
		if(result==-1){
			int err=errno;
			if(err!=EAGAIN && err!=EINTR){
				std::cerr << "select gave error " << err << std::endl;
				return false;
			}
		}
		else{
			if(direction==READ && FD_ISSET(fd_out,readset))
				return true;
			if(direction==WRITE && FD_ISSET(fd_in,writeset))
				return true;
			if(!wait)
				return false;
		}
	}
}

ProcessHandle::~ProcessHandle(){
	shutDown();
	//TODO: at this point we want to delete the child entry in exitStatuses
	//because it is useless. However, we can't do that here for certain because
	//we may not have yet recieved and handled the SIGCHLD. The correct thing to 
	//do is probably to store something more than just a char, here uprase_fn to 
	//either delete the entry or insert it as a tombstone, then, in 
	//reapProcesses uprase_fn and erase if a tombstone is found
}

void ProcessHandle::shutDown(){
	if(child){
		if(::kill(child,SIGTERM)){
			auto err=errno;
			if(err!=ESRCH){
				std::cerr << "Sending SIGTERM to child process failed, error code " 
				<< err << std::endl;
			}
		}
	}
}

bool ProcessHandle::done() const{
	assert(child && "child process muct not be detatched");
	return exitStatuses.contains(child);
}

char ProcessHandle::exitStatus() const{
	assert(child && "child process muct not be detatched");
	return exitStatuses.find(child);
}

void reapProcesses(){
	if(!reapFlag)
		return;
	int stat;
	pid_t p;
	while(true){
		p=waitpid(-1,&stat,WNOHANG);
		if(!p){ //great, done
			reapFlag=0;
			return;
		}
		if(p==-1){
			auto err=errno;
			if(err==ECHILD){ //great, done
				reapFlag=0;
				return;
			}
			if(err==EINTR)
				continue;
			else
				throw std::runtime_error("waitpid failed: "+std::to_string(err));
		}
		else{
			if(WIFEXITED(stat)) //if child exited normally
				exitStatuses.insert(p,WEXITSTATUS(stat));
			else //on termination by a signal or similar treat status as -1
				exitStatuses.insert(p,-1);
		}
	}
}

void startReaper(){
	std::thread reaper([](){
		while(true){
			reapProcesses();
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	});
	reaper.detach();
}

extern char **environ;

ProcessHandle startProcessAsync(std::string exe, const std::vector<std::string>& args, 
                                const std::map<std::string,std::string>& env, 
                                ForkCallbacks&& callbacks, bool detachable){
	//prepare arguments
	std::unique_ptr<const char*[]> rawArgs(new const char*[2+args.size()]);
	rawArgs[0]=exe.c_str();
	for(std::size_t i=0; i<args.size(); i++)
		rawArgs[i+1]=args[i].c_str();
	rawArgs[args.size()+1]=nullptr;
	
	//prepare environment variables
	std::unique_ptr<char*[]> newEnvData;
	std::vector<std::unique_ptr<char[]>> newEnvStrings;
	char** newEnv=environ;
	if(!env.empty()){
		//figure out how many unique variables there will be
		std::size_t nVars=0;
		for(char** ptr=environ; *ptr; ptr++){
			std::string entry(*ptr);
			std::string var=entry.substr(0,entry.find('='));
			//variables which also appear in env will be replaced
			if(!env.count(var))
				nVars++;
		}
		nVars+=env.size();
		//allocate space
		newEnvData.reset(new char*[nVars+1]);
		newEnvData[nVars]=nullptr;
		//copy data
		std::size_t idx=0;
		for(char** ptr=environ; *ptr; ptr++){
			std::string entry(*ptr);
			std::string var=entry.substr(0,entry.find('='));
			//variables which also appear in env will be replaced
			if(!env.count(var)){
				std::size_t len=strlen(*ptr)+1;
				newEnvStrings.emplace_back(new char[len]);
				strncpy(newEnvStrings.back().get(), *ptr, len);
				newEnvData[idx++]=newEnvStrings.back().get();
			}
		}
		for(const auto& entry : env){
			std::size_t len=entry.first.size()+1+entry.second.size()+1;
			newEnvStrings.emplace_back(new char[len]);
			snprintf(newEnvStrings.back().get(),len,"%s=%s",entry.first.c_str(),entry.second.c_str());
			newEnvData[idx++]=newEnvStrings.back().get();
		}
		assert(idx==nVars);
		newEnv=newEnvData.get();
	}
	//locate executable
	if(exe.find('/')==std::string::npos){
		//no slash; search through the path
		std::string defPath=_PATH_DEFPATH;
		fetchFromEnvironment("PATH",defPath);
		std::size_t idx=0, next;
		while(true){
			next=defPath.find(':',idx);
			std::string dir=defPath.substr(idx,next==std::string::npos?next:next-idx);
			std::string posExe=dir+'/'+exe;
			struct stat info;
			int err=stat(posExe.c_str(),&info);
			if(!err){
				exe=posExe;
				break;
			}
			if(next==std::string::npos)
				throw std::runtime_error("Unable to locate "+exe+" in default path ("+defPath+')');
			idx=next+1;
		}
	}
	else{
		//exe contains a slash, so we assume it a usable path. 
		//Check that the file exists.
		struct stat info;
		int err=stat(exe.c_str(),&info);
		if(err){
			err=errno;
			throw std::runtime_error("Cannot stat "+exe+": Error "+std::to_string(err));
		}
	}
	
	int err;
	//create communication pipes
	int inpipe[2];
	int outpipe[2];
	int errpipe[2];
	if(!detachable){
	err=pipe(inpipe);
		if(err){
			err=errno;
			throw std::runtime_error("Unable to allocate pipe: Error "+std::to_string(err));
		}
		err=pipe(outpipe);
		if(err){
			err=errno;
			throw std::runtime_error("Unable to allocate pipe: Error "+std::to_string(err));
		}
		err=pipe(errpipe);
		if(err){
			err=errno;
			throw std::runtime_error("Unable to allocate pipe: Error "+std::to_string(err));
		}
	}
	
	callbacks.beforeFork();
	pid_t child=fork();
	if(child<0){ //fork failed
		auto err=errno;
		std::cerr << "Failed to start child process: Error " << err << std::endl;
		return ProcessHandle{};
	}
	if(!child){ //if we don't know who the child is, it is us
		callbacks.inChild();
		//connect standard fds to pipes
		if(detachable){
			int nullfd=open("/dev/null",O_RDWR);
			dup2(nullfd,0);
			dup2(nullfd,1);
			dup2(nullfd,2);
		}
		else{
			dup2(inpipe[0],0);
			dup2(outpipe[1],1);
			dup2(errpipe[1],2);
		}
		//close all other fds
		for(int i = 3; i<FOPEN_MAX; i++)
			close(i);
		//be the child process
		execve(exe.c_str(),(char *const *)rawArgs.get(),(char *const *)newEnv);
		int err=errno;
		//not that this will be any help if we are detatchable
		fprintf(stderr,"Exec failed: Error %i\n",err);
	}
	//otherwise, we are still the parent
	callbacks.inParent();
	//close ends of pipes we will not use
	if(!detachable){
		close(inpipe[0]);
		close(outpipe[1]);
		close(errpipe[1]);
		return ProcessHandle(child,inpipe[1],outpipe[0],errpipe[0]);
	}
	return ProcessHandle(child);
}
