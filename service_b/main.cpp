// Includes
#include "crow.h"
#include <boost/algorithm/string.hpp>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp>
#include <iomanip>
#include <iostream>

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
        CROW_LOG_INFO << "city: " << city;
        CROW_LOG_INFO << "req.url: " << req.url;
        CROW_LOG_INFO << "req.raw_url: " << req.raw_url;
        CROW_LOG_INFO << "req.url_params: " << req.url_params;

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
        
        tm date_time = {};
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
                
        // Convert the parsed date to a time_t value
        from_date_time_t = mktime(&date_time);
        date_time.tm_mday += days;
        to_date_time_t = mktime(&date_time);

        std::vector<std::string> valid_units = {"c","f"};
        std::string unit = req.url_params.get("unit") ? req.url_params.get("unit") : "";
        unit = boost::algorithm::to_lower_copy(unit);
        boost::trim(unit);
        if (unit.empty() || (count(valid_units.begin(), valid_units.end(), unit) <= 0))
        {
            unit = "C";
        }

        std::vector<std::string> valid_aggs = {"daily","rolling7",""};
        std::string agg = req.url_params.get("agg") ? req.url_params.get("agg") : "";
        agg = boost::algorithm::to_lower_copy(agg);
        boost::trim(agg);
        if (count(valid_aggs.begin(), valid_aggs.end(), agg) <= 0)
        {
            unit = "";
        }
        
        CROW_LOG_INFO << "SERVICE B: city: " << city;
        char buff1[10];
        strftime(buff1, 10, "%Y-%m-%d", localtime(&from_date_time_t));
        CROW_LOG_INFO << "SERVICE B: date: " << buff1;
        CROW_LOG_INFO << "SERVICE B: days: " << days;
        CROW_LOG_INFO << "SERVICE B: unit: " << unit;        
        CROW_LOG_INFO << "SERVICE B: agg: " << agg;        


        CROW_LOG_INFO << "SERVICE A: city: " << city;
        char buff2[10];
        strftime(buff2, 10, "%Y-%m-%d", localtime(&from_date_time_t));
        CROW_LOG_INFO << "SERVICE A: from: " << buff2;
        strftime(buff2, 10, "%Y-%m-%d", localtime(&to_date_time_t));
        CROW_LOG_INFO << "SERVICE A: to: " << buff2;
        int page = 1;
        CROW_LOG_INFO << "SERVICE A: page: " << page;
        CROW_LOG_INFO << "SERVICE A: limit: " << days;

        crow::json::wvalue health_status;
        health_status["status"] = "ok";
        return crow::response{200, health_status};

    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    return 0;
}