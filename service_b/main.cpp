// Includes
#include "crow.h"
#include <boost/algorithm/string.hpp>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp>
#include <iomanip>
#include <iostream>

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

	namespace parser
	{
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
}

int main() {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/health")
	.methods("GET"_method)([]() {
		crow::json::wvalue health_status;
		health_status["status"] = "ok";
		return crow::response{200, health_status};
    });

    CROW_ROUTE(app, "/weather/<string>")
    .methods("GET"_method)([](const crow::request& req, std::string city) {
    	techtest::req_params_get_weather req_params(req, city);
		if (!req_params.is_valid)
		{
			return crow::response(500, req_params.error_response_str);
		}

		int limit = req_params.days;

        std::time_t from_date_time_t;
        std::time_t to_date_time_t;

        tm from_date_time = req_params.date;
		if (req_params.agg == "rolling7")
		{
	        from_date_time.tm_mday -= 6;
			limit += 6;
		}
        from_date_time_t = mktime(&from_date_time);
                
        tm to_date_time = req_params.date;
	    to_date_time.tm_mday += req_params.days - 1;
        to_date_time_t = mktime(&to_date_time);

        std::string from = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&from_date_time_t));
        std::string to = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&to_date_time_t));
        int page = 1;

        // RAII cleanup
        curlpp::Cleanup myCleanup;

        std::ostringstream os;
        std::ostringstream url;
        url << "service-a:8080/weather?city=" << req_params.city << "&from=" << from << "&to=" << to << "&page=" << page << "&limit=" << limit;
        os << curlpp::options::Url(url.str());
        std::string service_a_response = os.str();

        auto response_json_str = crow::json::load(service_a_response);
        crow::json::rvalue response_json(response_json_str);
        crow::json::rvalue day_list = response_json["days"];

		crow::json::wvalue response;
		switch(hash(req_params.agg))
		{
			case "rolling7"_hash:
				{
					std::vector<double> temp_rolling_mean;
					std::vector<double> cloudiness_rolling_mean;
					std::vector<double> precipitation_rolling_mean;
					
					for (int i = 0; i < req_params.days; i++)
					{
                        temp_rolling_mean.push_back((day_list[i]["temp_min"].d() + day_list[i]["temp_max"].d())/2.f);
                        cloudiness_rolling_mean.push_back(day_list[i]["cloudiness"].d());
                        precipitation_rolling_mean.push_back(day_list[i]["precipitation"].d());
					}

    				int days_number_offset = 1;
    				while (days_number_offset < 7)
    				{
					    for (int i = 0; i < req_params.days; i++)
					    {
                            temp_rolling_mean[i] += (day_list[i + days_number_offset]["temp_min"].d() + day_list[i + days_number_offset]["temp_max"].d())/2.;
                            cloudiness_rolling_mean[i] += day_list[i + days_number_offset]["cloudiness"].d();
                            precipitation_rolling_mean[i] += day_list[i + days_number_offset]["precipitation"].d();
					    }
						days_number_offset++;
 					}

					crow::json::wvalue days;
					for (int i = 0; i < req_params.days; i++)
					{
						crow::json::wvalue day;

	    				temp_rolling_mean[i] /= 7;
                        cloudiness_rolling_mean[i] /= 7;
                        precipitation_rolling_mean[i] /= 7;
						
						day["temp_rolling_mean"] = (req_params.unit == "C") ? temp_rolling_mean[i] : techtest::units::c_to_f(temp_rolling_mean[i]);
    	                day["cloudiness_rolling_mean"] = cloudiness_rolling_mean[i];
    	                day["precipitation_rolling_mean"] = precipitation_rolling_mean[i];

                        auto day_dump = crow::json::load(day.dump());
                        crow::json::rvalue day_r(day_dump);
    					
						CROW_LOG_INFO << " data: " << day_list[i + 6]["date"].s() << " i " << i << " temp_rolling_mean " << temp_rolling_mean[i] << " cloudiness_rolling_mean " << cloudiness_rolling_mean[i] << " precipitation_rolling_mean " << precipitation_rolling_mean[i];
    					days[day_list[i + 6]["date"].s()] = day_r;
					}

					auto days_dump = crow::json::load(days.dump());
                    crow::json::rvalue days_r(days_dump);
    				
					response["city"] = req_params.city;
					response["unit"] = req_params.unit;
					response["from"] = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&req_params.date_time_t));
					response["to"] = to;
				    response["days"] = days_r;
				}
				break;

			case "daily"_hash:
			default:
				{
        			crow::json::wvalue days;
    				for (int i = 0; i < day_list.size(); i++)
    				{
    					crow::json::wvalue day;
    					
    	                day["date"] = day_list[i]["date"].s();
    	                double temp_min = day_list[i]["temp_min"].d();
    	                day["temp_min"] = (req_params.unit == "C") ? temp_min : techtest::units::c_to_f(temp_min);
    	                double temp_max = day_list[i]["temp_max"].d();
    	                day["temp_max"] = (req_params.unit == "C") ? temp_max : techtest::units::c_to_f(temp_max);
    	                double temp_mean = (day_list[i]["temp_min"].d()+day_list[i]["temp_max"].d())/2.f;
    	                day["temp_mean"] = (req_params.unit == "C") ? temp_mean : techtest::units::c_to_f(temp_mean);
    	                day["precipitation"] = day_list[i]["precipitation"].d();
    	                day["cloudiness"] = day_list[i]["cloudiness"].d();
    					
                        auto day_dump = crow::json::load(day.dump());
                        crow::json::rvalue day_r(day_dump);
    
    					days[i] = day_r;
    				}

					auto days_dump = crow::json::load(days.dump());
                    crow::json::rvalue days_r(days_dump);
    
					response["city"] = req_params.city;
					response["unit"] = req_params.unit;
					response["from"] = techtest::parser::safe_strftime("%Y-%m-%d", std::localtime(&req_params.date_time_t));
					response["to"] = to;
				    response["days"] = days_r;
				}
				break;
		}

        return crow::response{200, response};
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    return 0;
}