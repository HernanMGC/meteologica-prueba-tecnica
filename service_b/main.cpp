// Includes
#include "crow.h"
#include "crow/middlewares/cors.h"
#include <boost/algorithm/string.hpp>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp>
#include <iomanip>
#include <iostream>
#include <hiredis/hiredis.h>
#include <openssl/sha.h>

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

namespace techtest
{
	constexpr int CACHE_EXPIRE_TIME = 60 * 10;
	namespace parser
	{
		bool replace(std::string& str, const std::string& from, const std::string& to) {
			size_t start_pos = str.find(from);
			if(start_pos == std::string::npos)
				return false;
			str.replace(start_pos, from.length(), to);
			return true;
		}

        std::string safe_strftime(const char *fmt, const std::tm *t)
		{
            std::size_t len = 10; // Adjust initial length as desired. Maybe based on the length of fmt?
            auto buff = std::make_unique<char[]>(len);
            while (std::strftime(buff.get(), len, fmt, t) == 0)
			{
                len *= 2;
                buff = std::make_unique<char[]>(len);
            }

            return std::string{buff.get()};
        }
	}

    namespace units
    {
        double c_to_f(double c)
        {   
            // using the conversion formula
            return ((c * 9.0 / 5.0) + 32.0);
        }
    }

	namespace checksum
	{
		std::string sha256(const std::string str)
		{
			unsigned char hash[SHA256_DIGEST_LENGTH];
            
			SHA256_CTX sha256;
			SHA256_Init(&sha256);
			SHA256_Update(&sha256, str.c_str(), str.size());
			SHA256_Final(hash, &sha256);
            
			std::stringstream ss;
            
			for(int i = 0; i < SHA256_DIGEST_LENGTH; i++){
				ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>( hash[i] );
			}

			return ss.str();
		}
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
			
			service_a_query_params(std::string in_city, std::string in_from, std::string in_to, int in_page, int in_limit) : city(in_city), from(in_from), to(in_to), page(in_page), limit(in_limit) {} 
		};

		std::string do_service_a_query(service_a_query_params query_params, redisContext& redis_context)
		{
			std::ostringstream url;
			std::ostringstream os;
			redisReply* reply;

			// Retrieve the value from Redis
			reply = (redisReply*)redisCommand(&redis_context, "GET name");
			if (reply->type == REDIS_REPLY_STRING) {
				std::cout << "Stored string in Redis: " << reply->str << std::endl;
			} else {
				std::cout << "Failed to retrieve the value." << std::endl;
			}
			freeReplyObject(reply);

			url << "service-a:8080/weather?city=" << query_params.city << "&from=" << query_params.from << "&to=" << query_params.to << "&page=" << query_params.page << "&limit=" << query_params.limit;
			std::string url_hash = checksum::sha256(url.str());

			std::string service_a_response;

			// Retrieve the value from Redis
			reply = (redisReply*)redisCommand(&redis_context, "GET %s", url_hash.c_str());
			CROW_LOG_INFO << "Try get cached Service A URL query: \"GET " << url_hash << "\"";
			if (reply->type == REDIS_REPLY_STRING) {
				service_a_response = reply->str;
				CROW_LOG_INFO << "Stored result in Redis: " << reply->str;
			} else {
				os << curlpp::options::Url(url.str());
				service_a_response = os.str();
				CROW_LOG_INFO << "No cache available. Query result: " << os.str();
				
				freeReplyObject(reply);

				// Set a value in Redis
				auto response_json_str = crow::json::load(service_a_response);
				crow::json::rvalue response_json(response_json_str);
				std::ostringstream response_json_ss;
				response_json_ss << response_json;
				reply = (redisReply*)redisCommand(&redis_context, "SET %s %s", url_hash.c_str(), service_a_response.c_str());
				CROW_LOG_INFO << "Caching Query result \"SET " << url_hash << " " << response_json_ss.str() << "\" ...";

				freeReplyObject(reply);
				reply = (redisReply*)redisCommand(&redis_context, "EXPIRE %s %d", url_hash.c_str(), techtest::CACHE_EXPIRE_TIME);
			}
			freeReplyObject(reply);

			return service_a_response;
		}
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
		    req_params_get_weather(const crow::request& req, std::string in_city)
		    {
		    	std::vector<std::string> error_strings = {};

                city = in_city;           
                boost::trim(city);
                if (city.empty())
                {
                    error_strings.push_back("\"city\" parameter is required.");
                }
                 
                std::string date_str = req.url_params.get("date") ? req.url_params.get("date") : "";
                boost::trim(date_str);
                if (date_str.empty())
                {
                    error_strings.push_back("\"date\" parameter is required.");
                }
                std::time_t date_time_t;

                // Create a string stream to parse the date string
                std::istringstream date_ss(date_str);
                        
                // Parse the date string using std::get_time
                date_ss >> std::get_time(&date, "%Y-%m-%d");
                        
                // Check if parsing was successful
                if (date_ss.fail())
                {
                    error_strings.push_back("\"date\" format was incorrect. Expected format is 'YYYY-MM-DD'.");
                }
    	        date_time_t = mktime(&date);
                days = 5;
                try
                {
                    days = req.url_params.get("days") ? std::min(10, std::max(1, std::stoi(req.url_params.get("days")))) : 1;
                }
                catch (std::exception const & ex)
                {
                    error_strings.push_back("\"days\" parameter needs to be an integer.");
                }
                
                agg = req.url_params.get("agg") ? req.url_params.get("agg") : "";
                agg = boost::algorithm::to_lower_copy(agg);
                boost::trim(agg);
                if (count(valid_aggs.begin(), valid_aggs.end(), agg) <= 0)
                {
                    agg = "";
                }
                
                unit = req.url_params.get("unit") ? req.url_params.get("unit") : "";
                unit = boost::algorithm::to_upper_copy(unit);
                boost::trim(unit);
                if (unit.empty() || (count(valid_units.begin(), valid_units.end(), unit) <= 0))
                {
                    unit = "C";
                }
            
		    	is_valid = error_strings.empty();
		    	
		    	if (!is_valid)
		    	{
		    		std::ostringstream error_os;
		    		
		    		int i = 1;
		    		while (i < error_strings.size())
		    		{
		    			error_os << " " << error_strings[i];
		    		}
            
		    		error_response_str = error_os.str();
		    	}
		    }
	};
}

int main() {

    // Enable CORS
    crow::App<crow::CORSHandler> app;

    // Customize CORS
    auto& cors = app.get_middleware<crow::CORSHandler>();

    // clang-format off
    cors
      .global()
        .headers("X-Custom-Header", "Upgrade-Insecure-Requests")
        .methods("POST"_method, "GET"_method)
      .prefix("/cors")
        .origin("example.com")
      .prefix("/nocors")
        .ignore();
    // clang-format on


	// Connect to the Redis server
	redisContext* redis_context = redisConnect("cache", 6379);
	if (redis_context == nullptr || redis_context->err) {
		if (redis_context) {
			CROW_LOG_ERROR << "Connection error: " << redis_context->errstr;
		} else {
			CROW_LOG_ERROR << "Connection error: can't allocate Redis redis_context";
		}
		CROW_LOG_ERROR << "UNEXPECTED ERROR";
	}

	redisReply* reply = (redisReply*)redisCommand(redis_context, "AUTH %s", std::getenv("REDIS_PASSWORD"));
	if (reply->type == REDIS_REPLY_ERROR) {
		/* Authentication failed */
	}
	freeReplyObject(reply);

    CROW_ROUTE(app, "/health")
	.methods("GET"_method)([&redis_context, &reply]() {
		crow::json::wvalue health_status;
		health_status["status"] = "ok";
		return crow::response{200, health_status};
    });

    CROW_ROUTE(app, "/weather/<string>")
    .methods("GET"_method)([&redis_context](const crow::request& req, std::string city) {
    	techtest::req_params_get_weather req_params(req, city);
		if (!req_params.is_valid)
		{
			return crow::response(500, req_params.error_response_str);
		}

    	crow::json::wvalue null_value_w(nullptr);
		auto null_value_dump = crow::json::load(null_value_w.dump());
		crow::json::rvalue null_value_r(null_value_dump);

        // RAII cleanup
        curlpp::Cleanup myCleanup;

		crow::json::wvalue response;
		switch(hash(req_params.agg))
		{
			case "rolling7"_hash:
			{
				int rolling_size = 7;
				int limit = rolling_size;

				tm req_params_date_from_time = req_params.date;
				std::time_t req_params_date_from_time_t = mktime(&req_params_date_from_time);
				std::string req_params_date_from_str = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&req_params_date_from_time_t));

				tm req_params_date_to_time = req_params.date;
				req_params_date_to_time.tm_mday += rolling_size - 1;
				std::time_t req_params_date_to_time_t = mktime(&req_params_date_to_time);
				std::string req_params_date_to_str = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&req_params_date_to_time_t));

				crow::json::wvalue days;

				for (int i = 0; i < rolling_size; i++)
				{
					std::time_t from_date_time_t;
					std::time_t to_date_time_t;
				
					tm from_date_time = req_params.date;
					from_date_time.tm_mday += i - (rolling_size - 1);
					from_date_time_t = mktime(&from_date_time);
						                
					tm to_date_time = req_params.date;
					to_date_time.tm_mday += i;
					to_date_time_t = mktime(&to_date_time);

					std::string from = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&from_date_time_t));
					std::string to = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&to_date_time_t));
					int page = 1;
					
					techtest::service_a::service_a_query_params query_params(req_params.city, from, to, page, limit);
					std::string service_a_response = techtest::service_a::do_service_a_query(query_params, *redis_context);

					auto response_json_str = crow::json::load(service_a_response);
					crow::json::rvalue response_json(response_json_str);
					crow::json::rvalue day_list = response_json["days"];

					if (day_list.size() != rolling_size) { continue; }

					double temp_rolling_mean = 0.f;
					double cloudiness_rolling_mean = 0.f;
					double precipitation_rolling_mean = 0.f;

					bool temp_min_is_null = false;
					bool temp_max_is_null = false;
					bool cloudiness_is_null = false;
					bool precipitation_is_null = false;

					int roll_index = 0;
					while (roll_index < rolling_size)
					{
						temp_min_is_null |= day_list[roll_index]["temp_min"].t() == crow::json::type::Null;
						temp_max_is_null |= day_list[roll_index]["temp_max"].t() == crow::json::type::Null;
						cloudiness_is_null |= day_list[roll_index]["cloudiness"].t() == crow::json::type::Null;
						precipitation_is_null |= day_list[roll_index]["precipitation"].t() == crow::json::type::Null;

						if (temp_min_is_null || temp_max_is_null)
						{
							temp_rolling_mean = 0.f;
						}
						else
						{
							temp_rolling_mean += (day_list[roll_index]["temp_min"].d() + day_list[roll_index]["temp_max"].d())/2.f;
						}
                    
						if (cloudiness_is_null)
						{
							cloudiness_rolling_mean = 0.f;
						}
						else
						{
							cloudiness_rolling_mean += day_list[roll_index]["cloudiness"].d();
						}
                    
						if (precipitation_is_null)
						{
							precipitation_rolling_mean = 0.f;
						}
						else
						{
							precipitation_rolling_mean += day_list[roll_index]["precipitation"].d();
						}

						roll_index++;
					}

					crow::json::wvalue day;
			
					temp_rolling_mean /= 7;
					cloudiness_rolling_mean /= 7;
					precipitation_rolling_mean /= 7;
					
					day["date"] = to;
					if (temp_min_is_null || temp_max_is_null)
					{
						day["temp_rolling_mean"] = null_value_r;
					}
					else
					{
						day["temp_rolling_mean"] = (req_params.unit == "C") ? temp_rolling_mean : techtest::units::c_to_f(temp_rolling_mean);
					}
					if (cloudiness_is_null)
					{
						day["cloudiness_rolling_mean"] = null_value_r;
					}
					else
					{
						day["cloudiness_rolling_mean"] = cloudiness_rolling_mean;
					
					}
					if (precipitation_is_null)
					{
						day["precipitation_rolling_mean"] = null_value_r;
					}
					else
					{
						day["precipitation_rolling_mean"] = precipitation_rolling_mean;
					}
			        
					auto day_dump = crow::json::load(day.dump());
					crow::json::rvalue day_r(day_dump);
					
					days[i] = day_r;
				}

				auto days_dump = crow::json::load(days.dump());
				crow::json::rvalue days_r(days_dump);
				
				response["city"] = req_params.city;
				response["unit"] = req_params.unit;
				response["from"] = req_params_date_from_str;
				response["to"] = req_params_date_to_str;
				response["days"] = days_r;
			}
			break;

			case "daily"_hash:
			default:
			{
				int limit = req_params.days;

				std::time_t req_params_date_time_t;
				std::time_t from_date_time_t;
				std::time_t to_date_time_t;

				tm req_params_date_time = req_params.date;
				req_params_date_time_t = mktime(&req_params_date_time);

				tm from_date_time = req_params.date;
				from_date_time_t = mktime(&from_date_time);
	                
				tm to_date_time = req_params.date;
				to_date_time.tm_mday += req_params.days - 1;
				to_date_time_t = mktime(&to_date_time);

				std::string req_params_date_str = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&req_params_date_time_t));
				std::string from = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&from_date_time_t));
				std::string to = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&to_date_time_t));
				int page = 1;

				techtest::service_a::service_a_query_params query_params(req_params.city, from, to, page, limit);
				std::string service_a_response = techtest::service_a::do_service_a_query(query_params, *redis_context);

				auto response_json_str = crow::json::load(service_a_response);
				crow::json::rvalue response_json(response_json_str);
				crow::json::rvalue day_list = response_json["days"];

        		crow::json::wvalue days;
    			for (int i = 0; i < day_list.size(); i++)
    			{
    				crow::json::wvalue day;

                    day["date"] = day_list[i]["date"].s();
                    
					if (day_list[i]["temp_min"].t() == crow::json::type::Null)
					{
						day["temp_min"] = null_value_r;
					}
					else
					{
						double temp_min = day_list[i]["temp_min"].d();
						day["temp_min"] = (req_params.unit == "C") ? temp_min : techtest::units::c_to_f(temp_min);
					}
					
    				if (day_list[i]["temp_max"].t() == crow::json::type::Null)
    				{
						day["temp_max"] = null_value_r;
					}
					else
					{
						double temp_max = day_list[i]["temp_max"].d();
    	                day["temp_max"] = (req_params.unit == "C") ? temp_max : techtest::units::c_to_f(temp_max);
                    }

    				if (day_list[i]["temp_min"].t() == crow::json::type::Null || day_list[i]["temp_max"].t() == crow::json::type::Null)
					{
    					day["temp_mean"] = null_value_r;
					}
					else
					{
						double temp_mean = (day_list[i]["temp_min"].d()+day_list[i]["temp_max"].d())/2.f;
						day["temp_mean"] = (req_params.unit == "C") ? temp_mean : techtest::units::c_to_f(temp_mean);
					}
                    
					if (day_list[i]["precipitation"].t() == crow::json::type::Null)
					{
						day["precipitation"] = null_value_r;
					}
					else
					{
						day["precipitation"] = day_list[i]["precipitation"].d();
					}
                    
					if (day_list[i]["cloudiness"].t() == crow::json::type::Null)
					{
						day["cloudiness"] = null_value_r;
					}
					else
					{
						day["cloudiness"] = day_list[i]["cloudiness"].d();
					}
    				
                    auto day_dump = crow::json::load(day.dump());
                    crow::json::rvalue day_r(day_dump);

    				days[i] = day_r;
    			}

				auto days_dump = crow::json::load(days.dump());
                crow::json::rvalue days_r(days_dump);

				response["city"] = req_params.city;
				response["unit"] = req_params.unit;
				response["from"] = req_params_date_str;
				response["to"] = to;
			    response["days"] = days_r;
			}
			break;
		}

        return crow::response{200, response};
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

	// Free the redis_context
	redisFree(redis_context);

    return 0;
}