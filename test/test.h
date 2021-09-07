#include <atomic>
#include <cmath>
#include <map>
#include <string>
#include <sstream>
#include <thread>

#include <rapidjson/schema.h>

#include <Entities.h>
#include <FileHandle.h>
#include <HTTPRequests.h>
#include <Process.h>

void emit_error(const std::string& file, size_t line,
				const std::string& criterion, const std::string& message="");

void emit_schema_error(const std::string& file, size_t line,
                       const rapidjson::SchemaValidator& validator, 
                       const std::string& message="");

#define ENSURE(cond,...) \
	do{ \
		if(!(cond)) \
			emit_error(__FILE__,__LINE__,#cond,##__VA_ARGS__); \
	}while(0)

namespace{
	template<typename T1, typename T2>
	std::string express_comparison(const std::string& e1, const T1& v1,
	                               const std::string& e2, const T2& v2){
		std::ostringstream ss;
		ss.precision(16);
		ss << e1 << " (" << v1 << ") != " << e2 << " (" << v2 << ")";
		return(ss.str());
	}
	template<typename T>
	std::string express_comparison(const std::string& e1, const T& v1,
	                               const std::string& e2, const T& v2,
	                               const T& tolerance){
		std::ostringstream ss;
		ss.precision(16);
		ss << e1 << " (" << v1 << ") != " << e1 << " (" << v2 << ")";
		if(tolerance!=0)
			ss << " to within " << tolerance;
		return(ss.str());
	}

	//compare floating point values for equality, treating NaNs as equal
	bool ensure_equal_impl(float v1, float v2){
		return(v1==v2 || (std::isnan(v1) && std::isnan(v2)));
	}
	//compare floating point values for equality, treating NaNs as equal
	bool ensure_equal_impl(double v1, double v2){
		return(v1==v2 || (std::isnan(v1) && std::isnan(v2)));
	}
	template<typename T1, typename T2>
	bool ensure_equal_impl(const T1& v1, const T2& v2){
		return(v1==v2);
	}
}

///Test that two quantities are equal and emit an error if they are not
///\param first first expression to test for equality
///\param second second expression to test for equality
///Optionally accepts an additional informative message to print along with an 
///emitted error.
#define ENSURE_EQUAL(first,second,...) \
	do{ \
		if(!ensure_equal_impl(first,second)) \
			emit_error(__FILE__,__LINE__, \
			  express_comparison(#first,first,#second,second),##__VA_ARGS__); \
	}while(0)

///Test that two quantities are equal to within a specified tolerance and emit 
///an error if they are not
///\param first first expression to test for equality
///\param second second expression to test for equality
///\param tolerance the maximum allowed difference between \p first and \p second
///Optionally accepts an additional informative message to print along with an 
///emitted error.
#define ENSURE_DISTANCE(first,second,tolerance,...) \
do{ \
	if(!(std::abs((first)-(second))<(tolerance))) \
		emit_error(__FILE__,__LINE__, \
		  express_comparison(#first,first,#second,second,tolerance),##__VA_ARGS__); \
}while(0)

#define ENSURE_CONFORMS(document,schema,...) \
do{ \
	rapidjson::SchemaValidator validator(schema); \
	if(!document.Accept(validator)) \
		emit_schema_error(__FILE__,__LINE__,validator,##__VA_ARGS__); \
}while(0)

///Unconditionally emit an error
///Optionally accepts an additional informative message to print along with the
///error.
#define FAIL(...) \
	emit_error(__FILE__,__LINE__,"FAIL",##__VA_ARGS__)

std::map<std::string,void(*)()>& test_registry();

struct register_test{
	register_test(const std::string& test_name, void(*test)()){
		test_registry().insert(std::make_pair(test_name,test));
	}
};

///Define a function to be run as a test
#define TEST(name) \
	void test_func ## name (); \
	static register_test register_ ## name (#name,&test_func ## name); \
	void test_func ## name ()

///Wait for an instance of the API server to be ready for requests, since 
///requests made before it begins listening on its port will be immediately 
///rejected by the OS
void waitServerReady(ProcessHandle& server);

class PersistentStore;

struct DatabaseContext{
public:
	DatabaseContext();
	~DatabaseContext();
	
	std::string getDBPort() const{ return dbPort; }
	std::string getPortalUserConfigPath() const{ return configDir.path()+"/slate_portal_user"; }
	std::string getEncryptionKeyPath() const{ return configDir.path()+"/encryptionKey"; }
	///Get the user record for the web-portal user
	const User& getPortalUser() const{ return baseUser; }
	///Fetch the web-portal user's user ID
	std::string getPortalUserID() const{ return baseUser.id; }
	///Fetch the web-portal user's administrator token
	std::string getPortalToken() const{ return baseUser.token; }
	std::unique_ptr<PersistentStore> makePersistentStore() const;
private:
	std::string dbPort;
	FileHandle configDir;
	User baseUser;
};

class PersistentStore;

struct TestContext{
public:
	explicit TestContext(std::vector<std::string> extraOptions={});
	~TestContext();
	std::string getAPIServerURL() const;
	std::string getKubeConfig();
	///Get the user record for the web-portal user
	const User& getPortalUser() const{ return db.getPortalUser(); }
	///Fetch the web-portal user's user ID
	std::string getPortalUserID() const{ return db.getPortalUserID(); }
	///Fetch the web-portal user's administrator token
	std::string getPortalToken() const{ return db.getPortalToken(); }
	std::unique_ptr<PersistentStore> makePersistentStore() const;
private:
	DatabaseContext db;
	std::string serverPort;
	std::string kubeconfig;
	std::string namespaceName;
	void waitServerReady();
public:
	ProcessHandle server;
private:
	class Logger{
		bool running;
		std::atomic<bool> stop;
		std::thread loggerThread;
	public:
		Logger();
		void start(ProcessHandle& server);
		~Logger();
	} logger;
};

std::string getSchemaDir();

rapidjson::SchemaDocument loadSchema(const std::string& path);

extern const std::string currentAPIVersion;
