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
    
        // RAII cleanup
        curlpp::Cleanup myCleanup;

        // Send request and get a result.
        // Here I use a shortcut to get it in a string stream ...

        std::ostringstream os;
        os << curlpp::options::Url(std::string("service-a:8080/health"));

        std::string asAskedInQuestion = os.str();

        health_status["status"] = asAskedInQuestion;
        return crow::response{200, health_status};
    });

    CROW_ROUTE(app, "/weather/<string>")
    .methods("GET"_method)([](const crow::request& req, std::string city) {
        boost::trim(city);
        if (city.empty())
        {
            return crow::response(500, "\"city\" parameter is required.");
        }
         
        std::string date = req.url_params.get("date") ? req.url_params.get("date") : "";
        boost::trim(date);
        if (date.empty())
        {
            return crow::response(500, "\"date\" parameter is required.");
        }

        int days = 5;
        try
        {
            days = req.url_params.get("days") ? std::min(10, std::max(1, std::stoi(req.url_params.get("days")))) : 1;
        }
        catch (std::exception const & ex)
        {
            return crow::response(500, "\"days\" parameter needs to be an integer.");
        }
		int limit = days;
        
        tm date_time = {};
        std::time_t input_date_time_t;
        std::time_t from_date_time_t;
        std::time_t to_date_time_t;
                
        // Create a string stream to parse the date string
        std::istringstream date_ss(date);
                
        // Parse the date string using std::get_time
        date_ss >> std::get_time(&date_time, "%Y-%m-%d");
                
        // Check if parsing was successful
        if (date_ss.fail())
        {
            return crow::response(500, "\"date\" format was incorrect. Expected format is 'YYYY-MM-DD'.");
        }

        std::vector<std::string> valid_aggs = {"daily","rolling7",""};
        std::string agg = req.url_params.get("agg") ? req.url_params.get("agg") : "";
        agg = boost::algorithm::to_lower_copy(agg);
        boost::trim(agg);
        if (count(valid_aggs.begin(), valid_aggs.end(), agg) <= 0)
        {
            agg = "";
        }


        tm from_date_time = date_time;
		if (agg == "rolling7")
		{
	        from_date_time.tm_mday -= 6;
			limit += 6;
		}
        from_date_time_t = mktime(&from_date_time);
                
        tm to_date_time = date_time;
	    to_date_time.tm_mday += days ;
        to_date_time_t = mktime(&to_date_time);

        std::vector<std::string> valid_units = {"c","f"};
        std::string unit = req.url_params.get("unit") ? req.url_params.get("unit") : "";
        unit = boost::algorithm::to_lower_copy(unit);
        boost::trim(unit);
        if (unit.empty() || (count(valid_units.begin(), valid_units.end(), unit) <= 0))
        {
            unit = "c";
        }
        CROW_LOG_INFO << "SERVICE B: city: " << city;
        std::string from = safe_strftime("%Y-%m-%d", std::localtime(&from_date_time_t));
        CROW_LOG_INFO << "SERVICE B: date: " << from;
        CROW_LOG_INFO << "SERVICE B: days: " << days;
        CROW_LOG_INFO << "SERVICE B: unit: " << unit;        
        CROW_LOG_INFO << "SERVICE B: agg: " << agg;        


        CROW_LOG_INFO << "SERVICE A: city: " << city;
        CROW_LOG_INFO << "SERVICE A: from: " << from;
        std::string to = safe_strftime("%Y-%m-%d", std::localtime(&to_date_time_t));
        CROW_LOG_INFO << "SERVICE A: to: " << to;
        int page = 1;
        CROW_LOG_INFO << "SERVICE A: page: " << page;
        CROW_LOG_INFO << "SERVICE A: limit: " << limit;

        // RAII cleanup
        curlpp::Cleanup myCleanup;

        std::ostringstream os;
        std::ostringstream url;
        url << "service-a:8080/weather?city=" << city << "&from=" << from << "&to=" << to << "&page=" << page << "&limit=" << limit; 
        os << curlpp::options::Url(url.str());
        std::string service_a_response = os.str();

        auto response_json = crow::json::load(service_a_response);
        crow::json::rvalue weather(response_json);
        crow::json::rvalue weather_line_list = weather["weather_lines"];


		crow::json::wvalue response;
		switch(hash(agg))
		{
			case "daily"_hash:
				{
    				crow::json::wvalue weather_line;
    				
                    double temp_min = weather_line_list[0]["temp_min"].d();
                    double temp_max = weather_line_list[0]["temp_max"].d();
                    double temp_mean = (weather_line_list[0]["temp_min"].d()+weather_line_list[0]["temp_max"].d())/2.f;
                    double precipitation = weather_line_list[0]["precipitation"].d();
                    double cloudiness = weather_line_list[0]["cloudiness"].d();
    				
    				int i = 1;
    				while (i < weather_line_list.size())
    				{
    	                CROW_LOG_INFO << "weather_line[" << i << "] - date " << weather_line_list[i]["date"];
    	                temp_min = std::min(temp_min, weather_line_list[i]["temp_min"].d());
    	                temp_max = std::max(temp_max, weather_line_list[i]["temp_max"].d());
    	                temp_mean += (weather_line_list[i]["temp_min"].d()+weather_line_list[i]["temp_max"].d())/2.f;
    	                precipitation += weather_line_list[i]["precipitation"].d();
    	                cloudiness += weather_line_list[i]["cloudiness"].d();
    					
    					i++;
    				}
                    
    				weather_line["temp_min"] = (unit == "c")?temp_min:c_to_f(temp_min);
                    weather_line["temp_max"] = (unit == "c")?temp_max:c_to_f(temp_max);
                    weather_line["temp_mean"] = (unit == "c")?temp_mean:c_to_f(temp_mean);
                    weather_line["precipitation"] = precipitation;
                    weather_line["cloudiness"] = cloudiness;
    				weather_line["temp_mean"] = temp_mean/weather_line_list.size();
    				weather_line["cloudiness"] = cloudiness/weather_line_list.size();
    
					auto weather_line_dump = crow::json::load(weather_line.dump());
                    crow::json::rvalue weather_line_r(weather_line_dump);

    				response["weather_agg_daily"] = weather_line_r;
				}

				break;

			case "rolling7"_hash:
				{
					std::vector<double> temp_rolling_mean;
					std::vector<double> cloudiness_rolling_mean;
					std::vector<double> precipitation_rolling_mean;
					

					CROW_LOG_INFO << "[0]";
					for (int i = 0; i < days; i++)
					{
                        temp_rolling_mean.push_back((weather_line_list[i]["temp_min"].d() + weather_line_list[i]["temp_max"].d())/2.f);
                        cloudiness_rolling_mean.push_back(weather_line_list[i]["cloudiness"].d());
                        precipitation_rolling_mean.push_back(weather_line_list[i]["precipitation"].d());
					}
					CROW_LOG_INFO << "[1]";

    				int days_offset = 1;
    				while (days_offset < 7)
    				{
						CROW_LOG_INFO << "[1]." << days_offset;
					    for (int i = 0; i < days; i++)
					    {
							CROW_LOG_INFO << "[1]." << days_offset << " " << i;
                            temp_rolling_mean[i] += (weather_line_list[i + days_offset]["temp_min"].d() + weather_line_list[i + days_offset]["temp_max"].d())/2.;
                            cloudiness_rolling_mean[i] += weather_line_list[i + days_offset]["cloudiness"].d();
                            precipitation_rolling_mean[i] += weather_line_list[i + days_offset]["precipitation"].d();
					    }
						days_offset++;
 					}
					CROW_LOG_INFO << "[2]";

					crow::json::wvalue weather_lines;
					for (int i = 0; i < days; i++)
					{
						crow::json::wvalue weather_line;

	    				temp_rolling_mean[i] /= 7;
                        cloudiness_rolling_mean[i] /= 7;
                        precipitation_rolling_mean[i] /= 7;
						
						weather_line["temp_rolling_mean"] = (unit == "c") ? temp_rolling_mean[i] : c_to_f(temp_rolling_mean[i]);
    	                weather_line["cloudiness_rolling_mean"] = cloudiness_rolling_mean[i];
    	                weather_line["precipitation_rolling_mean"] = precipitation_rolling_mean[i];

                        auto weather_line_dump = crow::json::load(weather_line.dump());
                        crow::json::rvalue weather_line_r(weather_line_dump);
    					
						CROW_LOG_INFO << " data: " << weather_line_list[i + 6]["date"].s() << " i " << i << " temp_rolling_mean " << temp_rolling_mean[i] << " cloudiness_rolling_mean " << cloudiness_rolling_mean[i] << " precipitation_rolling_mean " << precipitation_rolling_mean[i];
    					weather_lines[weather_line_list[i + 6]["date"].s()] = weather_line_r;
					}
					CROW_LOG_INFO << "[3]";

					auto weather_lines_dump = crow::json::load(weather_lines.dump());
                    crow::json::rvalue weather_lines_r(weather_lines_dump);
    
				    response["weather_lines_rolling7"] = weather_lines_r;
				}
				break;
			default:
				{
        			crow::json::wvalue weather_lines;
    				for (int i = 0; i < weather_line_list.size(); i++)
    				{
    					crow::json::wvalue weather_line;
    					
    	                weather_line["date"] = weather_line_list[i]["date"].s();
    	                double temp_min = weather_line_list[i]["temp_min"].d();
    	                weather_line["temp_min"] = (unit == "C") ? temp_min : c_to_f(temp_min);
    	                double temp_max = weather_line_list[i]["temp_max"].d();
    	                weather_line["temp_max"] = (unit == "C") ? temp_max : c_to_f(temp_max);
    	                double temp_mean = (weather_line_list[i]["temp_min"].d()+weather_line_list[i]["temp_max"].d())/2.f;
    	                weather_line["temp_mean"] = (unit == "C") ? temp_mean : c_to_f(temp_mean);
    	                weather_line["precipitation"] = weather_line_list[i]["precipitation"].d();
    	                weather_line["cloudiness"] = weather_line_list[i]["cloudiness"].d();
    					
                        auto weather_line_dump = crow::json::load(weather_line.dump());
                        crow::json::rvalue weather_line_r(weather_line_dump);
    
    					weather_lines[i] = weather_line_r;
    				}

					auto weather_lines_dump = crow::json::load(weather_lines.dump());
                    crow::json::rvalue weather_lines_r(weather_lines_dump);
    
				    response["weather_lines"] = weather_lines_r;
				}
				break;
		}

        return crow::response{200, response};
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    return 0;
}