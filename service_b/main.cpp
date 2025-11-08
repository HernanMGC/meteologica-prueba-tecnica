// Includes
#include "crow.h"
#include <boost/algorithm/string.hpp>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp>
#include <iomanip>
#include <iostream>

std::string safe_strftime(const char *fmt, const std::tm *t) {
    std::size_t len = 10; // Adjust initial length as desired. Maybe based on the length of fmt?
    auto buff = std::make_unique<char[]>(len);
    while (std::strftime(buff.get(), len, fmt, t) == 0) {
        len *= 2;
        buff = std::make_unique<char[]>(len);
    }
    return std::string{buff.get()};
}

constexpr uint64_t hash(std::string_view str) {
    uint64_t hash = 0;
    for (char c : str) {
        hash = (hash * 131) + c;
    }
    return hash;
}

constexpr uint64_t operator"" _hash(const char* str, size_t len) {
    return hash(std::string_view(str, len));
}

double c_to_f(double c)
{   
    // using the conversion formula
    return ((c * 9.0 / 5.0) + 32.0);
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
		int i= 0; 		
		CROW_LOG_INFO << i; i++;
        boost::trim(city);
        if (city.empty())
        {
            return crow::response(500, "\"city\" parameter is required.");
        }
    	CROW_LOG_INFO << i; i++;
         
        std::string date = req.url_params.get("date") ? req.url_params.get("date") : "";
        boost::trim(date);
        if (date.empty())
        {
            return crow::response(500, "\"date\" parameter is required.");
        }
    	CROW_LOG_INFO << i; i++;

        int days_number = 5;
        try
        {
            days_number = req.url_params.get("days") ? std::min(10, std::max(1, std::stoi(req.url_params.get("days")))) : 1;
        }
        catch (std::exception const & ex)
        {
            return crow::response(500, "\"days\" parameter needs to be an integer.");
        }
		int limit = days_number;
    	CROW_LOG_INFO << i; i++;
        
        tm date_time = {};
        std::time_t input_date_time_t;
        std::time_t from_date_time_t;
        std::time_t to_date_time_t;
                
        // Create a string stream to parse the date string
        std::istringstream date_ss(date);
                
        // Parse the date string using std::get_time
        date_ss >> std::get_time(&date_time, "%Y-%m-%d");
    	CROW_LOG_INFO << i; i++;
                
        // Check if parsing was successful
        if (date_ss.fail())
        {
            return crow::response(500, "\"date\" format was incorrect. Expected format is 'YYYY-MM-DD'.");
        }
    	CROW_LOG_INFO << i; i++;

        std::vector<std::string> valid_aggs = {"daily","rolling7",""};
        std::string agg = req.url_params.get("agg") ? req.url_params.get("agg") : "";
        agg = boost::algorithm::to_lower_copy(agg);
        boost::trim(agg);
        if (count(valid_aggs.begin(), valid_aggs.end(), agg) <= 0)
        {
            agg = "";
        }

    	CROW_LOG_INFO << i; i++;

        tm ini_from_date_time = date_time;
        tm from_date_time = date_time;
    	from_date_time_t = mktime(&ini_from_date_time);
		if (agg == "rolling7")
		{
	        from_date_time.tm_mday -= 6;
			limit += 6;
		}
        from_date_time_t = mktime(&from_date_time);
    	CROW_LOG_INFO << i; i++;
                
        tm to_date_time = date_time;
	    to_date_time.tm_mday += days_number - 1;
        to_date_time_t = mktime(&to_date_time);

        std::vector<std::string> valid_units = {"C","F"};
        std::string unit = req.url_params.get("unit") ? req.url_params.get("unit") : "";
        unit = boost::algorithm::to_upper_copy(unit);
        boost::trim(unit);
        if (unit.empty() || (count(valid_units.begin(), valid_units.end(), unit) <= 0))
        {
            unit = "C";
        }
        CROW_LOG_INFO << "SERVICE B: city: " << city;
    	std::string input_date = safe_strftime("%Y-%m-%d", std::localtime(&input_date_time_t));
        std::string from = safe_strftime("%Y-%m-%d", std::localtime(&from_date_time_t));
        CROW_LOG_INFO << "SERVICE B: date: " << from;
        CROW_LOG_INFO << "SERVICE B: days_number: " << days_number;
        CROW_LOG_INFO << "SERVICE B: unit: " << unit;        
        CROW_LOG_INFO << "SERVICE B: agg: " << agg;        
    	CROW_LOG_INFO << i; i++;


        CROW_LOG_INFO << "SERVICE A: city: " << city;
        CROW_LOG_INFO << "SERVICE A: from: " << from;
        std::string to = safe_strftime("%Y-%m-%d", std::localtime(&to_date_time_t));
        CROW_LOG_INFO << "SERVICE A: to: " << to;
        int page = 1;
        CROW_LOG_INFO << "SERVICE A: page: " << page;
        CROW_LOG_INFO << "SERVICE A: limit: " << limit;
    	CROW_LOG_INFO << i; i++;

        // RAII cleanup
        curlpp::Cleanup myCleanup;

        std::ostringstream os;
        std::ostringstream url;
        url << "service-a:8080/weather?city=" << city << "&from=" << from << "&to=" << to << "&page=" << page << "&limit=" << limit; 
        os << curlpp::options::Url(url.str());
        std::string service_a_response = os.str();

        auto response_json = crow::json::load(service_a_response);
        crow::json::rvalue weather(response_json);
        crow::json::rvalue day_list = weather["days"];
    	CROW_LOG_INFO << i; i++;


		crow::json::wvalue response;
		switch(hash(agg))
		{
			case "rolling7"_hash:
				{
					std::vector<double> temp_rolling_mean;
					std::vector<double> cloudiness_rolling_mean;
					std::vector<double> precipitation_rolling_mean;
					CROW_LOG_INFO << i; i++;
					

					CROW_LOG_INFO << "[0]";
					for (int i = 0; i < days_number; i++)
					{
                        temp_rolling_mean.push_back((day_list[i]["temp_min"].d() + day_list[i]["temp_max"].d())/2.f);
                        cloudiness_rolling_mean.push_back(day_list[i]["cloudiness"].d());
                        precipitation_rolling_mean.push_back(day_list[i]["precipitation"].d());
					}
					CROW_LOG_INFO << "[1]";

    				int days_number_offset = 1;
    				while (days_number_offset < 7)
    				{
						CROW_LOG_INFO << "[1]." << days_number_offset;
					    for (int i = 0; i < days_number; i++)
					    {
							CROW_LOG_INFO << "[1]." << days_number_offset << " " << i;
                            temp_rolling_mean[i] += (day_list[i + days_number_offset]["temp_min"].d() + day_list[i + days_number_offset]["temp_max"].d())/2.;
                            cloudiness_rolling_mean[i] += day_list[i + days_number_offset]["cloudiness"].d();
                            precipitation_rolling_mean[i] += day_list[i + days_number_offset]["precipitation"].d();
					    }
						days_number_offset++;
 					}
					CROW_LOG_INFO << "[2]";

					crow::json::wvalue days;
					for (int i = 0; i < days_number; i++)
					{
						crow::json::wvalue day;

	    				temp_rolling_mean[i] /= 7;
                        cloudiness_rolling_mean[i] /= 7;
                        precipitation_rolling_mean[i] /= 7;
						
						day["temp_rolling_mean"] = (unit == "C") ? temp_rolling_mean[i] : c_to_f(temp_rolling_mean[i]);
    	                day["cloudiness_rolling_mean"] = cloudiness_rolling_mean[i];
    	                day["precipitation_rolling_mean"] = precipitation_rolling_mean[i];

                        auto day_dump = crow::json::load(day.dump());
                        crow::json::rvalue day_r(day_dump);
    					
						CROW_LOG_INFO << " data: " << day_list[i + 6]["date"].s() << " i " << i << " temp_rolling_mean " << temp_rolling_mean[i] << " cloudiness_rolling_mean " << cloudiness_rolling_mean[i] << " precipitation_rolling_mean " << precipitation_rolling_mean[i];
    					days[day_list[i + 6]["date"].s()] = day_r;
					}
					CROW_LOG_INFO << "[3]";

					auto days_dump = crow::json::load(days.dump());
                    crow::json::rvalue days_r(days_dump);
    				
					response["city"] = city;
					response["unit"] = unit;
					response["from"] = input_date;
					response["to"] = to;
				    response["days"] = days_r;
				}
				break;
			CROW_LOG_INFO << i; i++;

			case "daily"_hash:
			default:
				{
        			crow::json::wvalue days;
    				for (int i = 0; i < day_list.size(); i++)
    				{
    					crow::json::wvalue day;
    					
    	                day["date"] = day_list[i]["date"].s();
    	                double temp_min = day_list[i]["temp_min"].d();
    	                day["temp_min"] = (unit == "C") ? temp_min : c_to_f(temp_min);
    	                double temp_max = day_list[i]["temp_max"].d();
    	                day["temp_max"] = (unit == "C") ? temp_max : c_to_f(temp_max);
    	                double temp_mean = (day_list[i]["temp_min"].d()+day_list[i]["temp_max"].d())/2.f;
    	                day["temp_mean"] = (unit == "C") ? temp_mean : c_to_f(temp_mean);
    	                day["precipitation"] = day_list[i]["precipitation"].d();
    	                day["cloudiness"] = day_list[i]["cloudiness"].d();
    					
                        auto day_dump = crow::json::load(day.dump());
                        crow::json::rvalue day_r(day_dump);
    
    					days[i] = day_r;
    				}

					auto days_dump = crow::json::load(days.dump());
                    crow::json::rvalue days_r(days_dump);
    
					response["city"] = city;
					response["unit"] = unit;
					response["from"] = input_date;
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