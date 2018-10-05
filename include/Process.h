#ifndef SLATE_PROCESS_H
#define SLATE_PROCESS_H

#include <cerrno>
#include <istream>
#include <map>
#include <ostream>
#include <streambuf>
#include <vector>

#include <unistd.h>

///Put a filedescriptor in non-blocking mode
///\fd the file descriptor to be made non-blocking
void setNonblocking(int fd);

struct ProcessIOBuffer : public std::basic_streambuf<char, std::char_traits<char> >{
public:
	///Default construct a buffer which has no associated file descriptors
	ProcessIOBuffer();
	
	///Construct with file descriptors
	///\param in fd from which data will be read
	///\param out fd to which data will be written
	ProcessIOBuffer(int in, int out);
	
	///Copy construction is forbidden
	ProcessIOBuffer(const ProcessIOBuffer&)=delete;
	
	///Move constructor
	ProcessIOBuffer(ProcessIOBuffer&& other);
	
	///Destructor
	~ProcessIOBuffer();
	
	///Copy assignment is forbidden
	ProcessIOBuffer& operator=(const ProcessIOBuffer&)=delete;
	
	///Move assignment
	ProcessIOBuffer& operator=(ProcessIOBuffer&& other);
	
	///Write data
	virtual std::streamsize xsputn(const char_type* s, std::streamsize n);
	
	///Read data to buffer
	traits_type::int_type underflow();
	
	///Check how many more characters may be available
	std::streamsize showmanyc();
	
	///Close the input file descriptor to signal to the reader that the data has 
	///ended. No further data can be written via xsputn after this function has 
	///been called. 
	void endInput();
	
private:
	const static std::size_t bufferSize=4096;

	int fd_in;
	int fd_out;
	char_type* readBuffer;
	bool readEOF;
	bool closedIn;
	
	enum rw{READ,WRITE};
	
	///Wait/check whether the underlying fd is ready for input or output
	///\param direction which fd to check
	///\param wait whether to sleep until the fd is ready, or return without blocking
	///\return whether the fd is ready; may be false if \p wait is false
	bool waitReady(rw direction, bool wait=true);
};

///An object for managing a child process
struct ProcessHandle{
public:
	ProcessHandle():child(0),in(&inoutBuf),out(&inoutBuf),err(&errBuf){}
	
	//construct a handle with ownership of a process but no means to communicate 
	//with it.
	explicit ProcessHandle(pid_t c):
	child(c),
	in(&inoutBuf),out(&inoutBuf),err(&errBuf)
	{}
	
	//construct a handle with ownership of a process and file descriptors for
	//comminicating with it
	ProcessHandle(pid_t c, int in, int out, int err):
	child(c),
	inoutBuf(in,out),errBuf(-1,err),
	in(&inoutBuf),out(&inoutBuf),err(&errBuf){}
	ProcessHandle(const ProcessHandle&)=delete;
	
	ProcessHandle(ProcessHandle&& other):
	child(other.child),
	inoutBuf(std::move(other.inoutBuf)),
	errBuf(std::move(other.errBuf)),
	in(&inoutBuf),
	out(&inoutBuf),
	err(&errBuf){
		other.child=0;
	}
	
	~ProcessHandle();
	ProcessHandle& operator=(ProcessHandle&)=delete;
	ProcessHandle& operator=(ProcessHandle&& other){
		if(&other!=this){
			shutDown();
			child=other.child;
			other.child=0;
			inoutBuf=std::move(other.inoutBuf);
			errBuf=std::move(other.errBuf);
		}
		return *this;
	}
	operator bool() const{ return child; }
	
	pid_t getPid() const{ return child; }
	///Get the stream connected to the child process's stdin
	///Not valid if the child was launched detachably
	std::ostream& getStdin(){ return(in); }
	///Get the stream connected to the child process's stdout
	///Not valid if the child was launched detachably
	std::istream& getStdout(){ return(out); }
	///Get the stream connected to the child process's stderr
	///Not valid if the child was launched detachably
	std::istream& getStderr(){ return(err); }
	///Close the stream to the child process's stdin
	void endInput(){ inoutBuf.endInput(); }
	///Give up responsibility for stopping the child process
	void detach(){
		child=0;
	}
	void kill(){
		shutDown();
	}
	///Only valid if the child process has not been detached
	bool done() const;
	///Only valid if the child process has not been detached and done() is true
	char exitStatus() const;
private:
	pid_t child;
	ProcessIOBuffer inoutBuf, errBuf;
	std::ostream in;
	std::istream out, err;
	
	friend ProcessHandle startProcessAsync(std::string, const std::vector<std::string>&);
	
	///Terminate the child process if it is still running
	void shutDown();
};

///Reap any child processes which have exited
void reapProcesses();
///Spawn a separate thread to periodically run reapProcesses()
void startReaper();
//Stop the background reaping thread
void stopReaper();

struct ForkCallbacks{
	///Called immediately before fork().
	virtual void beforeFork(){}
	///Called immediately after fork() in the child process
	virtual void inChild(){}
	///Called immediately after fork() in the parent process
	virtual void inParent(){}
};

///Start a child process and leave it running. 
///\param exe executable to start
///\param args arguments to pass to \p exe. \p exe will be automatically 
///            prepended as argv[0]
///\param callbacks a callback object for actions which must be taken 
///                 immediately before and after calling fork
///\param detachable whether the child process should be started in a detachable 
///                  state. Being detachable means that no communication will be
///                  possible with the child. 
ProcessHandle startProcessAsync(std::string exe, 
                                const std::vector<std::string>& args, 
                                const std::map<std::string,std::string>& env={}, 
                                ForkCallbacks&& callbacks=ForkCallbacks{}, 
                                bool detachable=false);

struct commandResult{
	std::string output;
	std::string error;
	int status;
};

///Run an external command
///\param command the command to be run. If \p command contains no slashes, a  
///               search will be performed in all entries of $PATH (or 
///               _PATH_DEFPATH if $PATH is not set) for a file with a matching 
///               name. 
///\param args the arguments to be passed to the child command
///\param env additions and changes to the child command's environment. These 
///           are added to the current process's environment to form the full
///           child environment. 
///\return a structure containing all data written by the child process to its
///        standard ouput and error and the child process's exit status
commandResult runCommand(const std::string& command, 
                         const std::vector<std::string>& args={}, 
                         const std::map<std::string,std::string>& env={});

///Run an external command, sending given data to its standard input
///\param command the command to be run. If \p command contains no slashes, a  
///               search will be performed in all entries of $PATH (or 
///               _PATH_DEFPATH if $PATH is not set) for a file with a matching 
///               name. 
///\param input the data which will be written to the child command's standard input
///\param args the arguments to be passed to the child command
///\param env additions and changes to the child command's environment. These 
///           are added to the current process's environment to form the full
///           child environment. 
///\return a structure containing all data written by the child process to its
///        standard ouput and error and the child process's exit status
commandResult runCommandWithInput(const std::string& command, 
                                  const std::string& input,
                                  const std::vector<std::string>& args={}, 
                                  const std::map<std::string,std::string>& env={});

#endif //SLATE_PROCESS_H