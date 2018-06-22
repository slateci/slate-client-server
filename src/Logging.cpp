#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/local_time_adjustor.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>

std::string timestamp(){
	auto now = boost::posix_time::second_clock::universal_time();
	return "["+to_simple_string(now)+" UTC] ";
}
