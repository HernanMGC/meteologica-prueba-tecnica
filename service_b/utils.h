//// Includes
#include <ctime>
#include <stdint.h>
#include <string>
#include <vector>

//// ForwardDeclarations
// CROW
namespace crow
{
	class request;
}

// HIREDIS
class redisContext;

////////////////////////////////////////////////////////////////////
// hash select feature
////////////////////////////////////////////////////////////////////

constexpr uint64_t hash(std::string_view str)
{
	uint64_t hash = 0;
	for (char c : str)
	{
		hash = (hash * 131) + c;
	}
	return hash;
}

constexpr uint64_t operator"" _hash(const char* str, size_t len)
{
	return hash(std::string_view(str, len));
}

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////

namespace techtest
{
	constexpr int CACHE_EXPIRE_TIME = 60 * 10;
	namespace parser
	{
		bool replace(std::string& str, const std::string& from, const std::string& to);

        std::string safe_strftime(const char *fmt, const std::tm *t);
	}

    namespace units
    {
        double c_to_f(double c);
    }

	namespace checksum
	{
		std::string sha256(const std::string str);
	}

	namespace service_a
	{
		struct service_a_query_params
		{
			std::string city = "";
			std::string from = "";
			std::string to = "";
			int page = 0;
			int limit = 0;
			
			service_a_query_params(std::string in_city, std::string in_from, std::string in_to, int in_page, int in_limit); 
		};

		std::string do_service_a_query(service_a_query_params query_params, redisContext& redis_context);
	}

	struct req_params_get_weather
	{
		
		public:
			std::string city = "";
			tm date = {};
            std::time_t date_time_t;
			int days = 0;
			std::string agg = "";
			std::string unit = "";
		    bool is_valid = false;
		    std::string error_response_str;
		
		private:
			std::vector<std::string> valid_aggs = {"daily", "rolling7"};
			std::vector<std::string> valid_units = {"C", "F"};

        public:
		    req_params_get_weather(const crow::request& req, std::string in_city);
	};
}
