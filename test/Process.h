#ifndef SLATE_PROCESS_H
#define SLATE_PROCESS_H

#include <cerrno>
#include <istream>
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
	
private:
	const static std::size_t bufferSize=4096;

	int fd_in;
	int fd_out;
	char_type* readBuffer;
	bool readEOF;
	
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
	
	~ProcessHandle(){
		shutDown();
	}
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
	///Give up responsibility for stopping the child process
	void detach(){
		child=0;
	}
	void kill(){
		shutDown();
	}
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
                                ForkCallbacks&& callbacks=ForkCallbacks{}, 
                                bool detachable=false);

#endif //SLATE_PROCESS_H