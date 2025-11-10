//// Includes
// TECHTEST
#include "utils.h"

// HIREDIS
#include <hiredis/hiredis.h>

#include "crow.h"
#include "crow/middlewares/cors.h"
#include <iomanip>
#include <iostream>
#include <openssl/sha.h>

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

        crow::json::wvalue response;
		switch (hash(req_params.agg))
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