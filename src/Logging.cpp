#include <boost/date_time/posix_time/posix_time.hpp>

std::string timestamp(){
	auto now = boost::posix_time::second_clock::universal_time();
	return "["+to_simple_string(now)+" UTC] ";
}
