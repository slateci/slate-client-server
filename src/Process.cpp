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
		sigemptyset(&act.sa_mask);
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
	
std::atomic<bool> reaperStop;
cuckoohash_map<pid_t,ProcessRecord> processTable;
} //anonymous namespace

ProcessIOBuffer::ProcessIOBuffer():
fd_in(-1),fd_out(-1),
readBuffer(nullptr),
readEOF(true),closedIn(true){
	setg(readBuffer,readBuffer,readBuffer);
}

ProcessIOBuffer::ProcessIOBuffer(int in, int out):
fd_in(in),fd_out(out),
readBuffer(new char_type[bufferSize]),
readEOF(false),closedIn(false){
	if(in!=-1)
		setNonblocking(in);
	if(out!=-1)
		setNonblocking(out);
	setg(readBuffer,readBuffer,readBuffer);
}

ProcessIOBuffer::ProcessIOBuffer(ProcessIOBuffer&& other):
fd_in(other.fd_in),fd_out(other.fd_out),
readBuffer(other.readBuffer),
readEOF(other.readEOF),closedIn(other.closedIn){
	setg(readBuffer,readBuffer,readBuffer);
	other.fd_in=-1;
	other.fd_out=-1;
	other.readEOF=true;
	other.closedIn=true;
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
		swap(closedIn,other.closedIn);
		setg(readBuffer,readBuffer,readBuffer);
		other.setg(other.readBuffer,other.readBuffer,other.readBuffer);
	}
	return *this;
}

std::streamsize ProcessIOBuffer::xsputn(const char_type* s, std::streamsize n){
	std::streamsize amountWritten=0;
	if(closedIn)
		return(amountWritten);
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

void ProcessIOBuffer::endInput(){
	close(fd_in);
	fd_in=-1;
	closedIn=true;
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
				if(err==9){
					std::cerr << "fds were: fd_out=" << fd_out << " fd_in=" 
					<< fd_in << " maxfd=" << maxfd << " direction=" 
					<< (direction==READ ? "READ" : "WRITE") << std::endl;
				}
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

ProcessHandle::ProcessHandle(pid_t c):
child(c),
in(&inoutBuf),out(&inoutBuf),err(&errBuf),
hasExitStatus(false)
{
	if(child){
		processTable.uprase_fn(child,[this](ProcessRecord& record){
			assert(!record.handle && "PID collision");
			this->setExitStatus(record.exitStatus);
			return true;
		},ProcessRecord(this));
	}
}

ProcessHandle::ProcessHandle(pid_t c, int in, int out, int err):
child(c),
inoutBuf(in,out),errBuf(-1,err),
in(&inoutBuf),out(&inoutBuf),err(&errBuf),
hasExitStatus(false)
{
	if(child){
		processTable.uprase_fn(child,[this](ProcessRecord& record){
			assert(!record.handle && "PID collision");
			this->setExitStatus(record.exitStatus);
			return true;
		},ProcessRecord(this));
	}
}

ProcessHandle::ProcessHandle(ProcessHandle&& other):
child(other.child),
inoutBuf(std::move(other.inoutBuf)),
errBuf(std::move(other.errBuf)),
in(&inoutBuf),
out(&inoutBuf),
err(&errBuf){
	other.child=0;
	if(child){
		ProcessHandle* oldPtr=&other;
		processTable.update_fn(child,[this,oldPtr](ProcessRecord& record){
			if(record.handle==oldPtr)
				record.handle=this;
			//take these values while holding the lock on this table entry to
			//avoid racing the reaper thread setting them
			if((this->hasExitStatus=oldPtr->hasExitStatus.load()))
				this->exitStatusValue=oldPtr->exitStatusValue;
		});
	}
}

ProcessHandle& ProcessHandle::operator=(ProcessHandle&& other){
	if(&other!=this){
		shutDown();
		child=other.child;
		other.child=0;
		inoutBuf=std::move(other.inoutBuf);
		errBuf=std::move(other.errBuf);
		if(child){
			ProcessHandle* oldPtr=&other;
			processTable.update_fn(child,[this,oldPtr](ProcessRecord& record){
				if(record.handle==oldPtr)
					record.handle=this;
				//take these values while holding the lock on this table entry to
				//avoid racing the reaper thread setting them
				if((this->hasExitStatus=oldPtr->hasExitStatus.load()))
					this->exitStatusValue=oldPtr->exitStatusValue;
			});
		}
	}
	return *this;
}

ProcessHandle::~ProcessHandle(){
	shutDown();
	if(child)
		processTable.erase_fn(child,[this](ProcessRecord& record){
			if(record.handle==this){
				if(this->hasExitStatus)
					return true;
				record.handle=nullptr;
				return false;
			}
			return false; //Is this reachable?
		});
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
	assert(child && "child process must not be detatched");
	return hasExitStatus;
}

char ProcessHandle::exitStatus() const{
	assert(child && "child process must not be detatched");
	assert(hasExitStatus && "child process must have completed");
	return exitStatusValue;
}

void ProcessHandle::setExitStatus(unsigned char status){
	exitStatusValue=status;
	hasExitStatus=true;
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
			unsigned char exitStatus;
			if(WIFEXITED(stat)) //if child exited normally
				exitStatus=WEXITSTATUS(stat);
			else //on termination by a signal or similar treat status as 255
				exitStatus=255;
			processTable.uprase_fn(p,[exitStatus,p](ProcessRecord& record){
				if(record.handle){
					//if the handle exists we can put the exit status into it
					record.handle->setExitStatus(exitStatus);
				}
				return true; //delete record
			},ProcessRecord(exitStatus));
		}
	}
}

void startReaper(){
	reaperStop.store(false);
	std::thread reaper([](){
		while(!reaperStop.load()){
			reapProcesses();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		//set the flag back to its original state to signal stopping
		reaperStop.store(false);
	});
	reaper.detach();
}

void stopReaper(){
	reaperStop.store(true);
	//wait for background thread to signal that it has indeed stopped
	while(!reaperStop.load()){
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

extern char **environ;

ProcessHandle startProcessAsync(std::string exe, const std::vector<std::string>& args, 
                                const std::map<std::string,std::string>& env, 
                                ForkCallbacks&& callbacks, bool detachable){
	assert(!reaperStop.load() && "Process reaper must be running");
	//prepare arguments
	std::unique_ptr<const char*[]> rawArgs(new const char*[2+args.size()]);
	//do not set argv[0] just yet, we may have to look through PATH to decide exactly what it is
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
			throw std::runtime_error("Cannot stat "+exe+": Error "+std::to_string(err)+": "+strerror(err));
		}
	}
	//set argv[0] now that we are sure we know what it is
	rawArgs[0]=exe.c_str();
	
	struct FdCloser{
		int& fd;
		bool active;
		FdCloser(int& fd):fd(fd),active(true){}
		~FdCloser(){ if(active && fd!=-1){ close(fd); }};
		int take(){ active=false; return fd; }
	};
	
	int err;
	//create communication pipes
	int inpipe[2]={-1,-1};
	int outpipe[2]={-1,-1};
	int errpipe[2]={-1,-1};
	FdCloser incloser[2]={{inpipe[0]},{inpipe[1]}};
	FdCloser outcloser[2]={{outpipe[0]},{outpipe[1]}};
	FdCloser errcloser[2]={{errpipe[0]},{errpipe[1]}};
	if(!detachable){
		err=pipe(inpipe);
		if(err){
			err=errno;
			throw std::runtime_error("Unable to allocate pipe: Error "+std::to_string(err)+": "+strerror(err));
		}
		err=pipe(outpipe);
		if(err){
			err=errno;
			throw std::runtime_error("Unable to allocate pipe: Error "+std::to_string(err)+": "+strerror(err));
		}
		err=pipe(errpipe);
		if(err){
			err=errno;
			throw std::runtime_error("Unable to allocate pipe: Error "+std::to_string(err)+": "+strerror(err));
		}
	}
	
	callbacks.beforeFork();
	pid_t child=fork();
	if(child<0){ //fork failed
		auto err=errno;
		throw std::runtime_error("Failed to start child process: Error "+std::to_string(err)+": "+strerror(err));
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
		exit(err);
	}
	//otherwise, we are still the parent
	callbacks.inParent();
	//close ends of pipes we will not use
	if(!detachable)
		return ProcessHandle(child,incloser[1].take(),outcloser[0].take(),errcloser[0].take());
	return ProcessHandle(child);
}


namespace{
	void collectChildOutput(ProcessHandle& child, commandResult& result){
		std::unique_ptr<char[]> buf(new char[1024]);
		char* bufptr=buf.get();
		//collect stdout
		//std::cout << "collecting child stdout" << std::endl;
		std::istream& child_stdout=child.getStdout();
		while(!child_stdout.eof()){
			char* ptr=bufptr;
			child_stdout.read(ptr,1);
			ptr+=child_stdout.gcount();
			child_stdout.readsome(ptr,1023);
			ptr+=child_stdout.gcount();
			result.output.append(bufptr,ptr-bufptr);
		}
		//collect stderr
		//std::cout << "collecting child stderr" << std::endl;
		std::istream& child_stderr=child.getStderr();
		while(!child_stderr.eof()){
			char* ptr=bufptr;
			child_stderr.read(ptr,1);
			ptr+=child_stderr.gcount();
			child_stderr.readsome(ptr,1023);
			ptr+=child_stderr.gcount();
			result.error.append(bufptr,ptr-bufptr);
		}
		//std::cout << "waiting for child exit" << std::endl;
		while(!child.done())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		result.status=child.exitStatus();
	}
}

commandResult runCommand(const std::string& command, 
                         const std::vector<std::string>& args,
                         const std::map<std::string,std::string>& env){
	commandResult result;
	ProcessHandle child=startProcessAsync(command,args,env);
	collectChildOutput(child,result);
	return result;
}

commandResult runCommandWithInput(const std::string& command, 
                                  const std::string& input,
                                  const std::vector<std::string>& args,
                                  const std::map<std::string,std::string>& env){
	commandResult result;
	ProcessHandle child=startProcessAsync(command,args,env);
	child.getStdin() << input;
	child.getStdin().flush();
	child.endInput();
	collectChildOutput(child,result);
	return result;
}
