// Includes
#include "crow.h"
#include "crow/middlewares/cors.h"
#include "mysql_driver.h"
#include <cppconn/prepared_statement.h>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/sha.h>

enum EWeatherTableCols
{
	NO_COLUMN = 0,
    DATE,
    CITY,
    TEMP_MAX,
    TEMP_MIN,
    PRECIPITATION,
    CLOUDINESS,
	MAX
};

namespace techtest
{
	struct req_params_get_weather
	{
        public:
		    std::string city = "";
		    std::string from = "";
		    std::string to = "";
		    int page = 0;
		    int limit = 0;
		    bool is_valid = false;
		    std::string error_response_str;
        
        public:
		    req_params_get_weather(const crow::request& req)
		    {
		    	std::vector<std::string> error_strings = {};
            
                city = req.url_params.get("city") ? req.url_params.get("city") : "";
                boost::trim(city);
                if (city.empty())
                {
                    error_strings.push_back("\"city\" parameter is required.");
                }
                
                from = req.url_params.get("from") ? req.url_params.get("from") : "";
                boost::trim(from);
                if (from.empty())
                {
                    error_strings.push_back("\"from\" parameter is required.");
                }
            
                to = req.url_params.get("to") ? req.url_params.get("to") : "";
                boost::trim(to);
                if (to.empty())
                {
                    error_strings.push_back("\"to\" parameter is required.");
                }
            
                if (from > to)
                {
                    error_strings.push_back("\"from\" date needs to be lower before then \"to\" date.");
                }
            
                page = 1;
                try
                {
                    page = req.url_params.get("page") ? std::max(1, std::stoi(req.url_params.get("page"))) : 1;
                }
                catch (std::exception const & ex)
                {
                    error_strings.push_back("\"page\" parameter needs to be an integer.");
                }
                
                limit = 10;
                try
                {
                    limit = req.url_params.get("limit") ? std::max(1, std::stoi(req.url_params.get("limit"))) : 10;
                }
                catch (std::exception const & ex)
                {
                    error_strings.push_back("\"limit\" parameter needs to be an integer.");
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
        bool replace(std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = str.find(from);
            if(start_pos == std::string::npos)
                return false;
            str.replace(start_pos, from.length(), to);
            return true;
        }

        bool parse_date(std::string DateStr, std::time_t& OutDate)
        {
            tm date_time = {};
            std::time_t date_time_t;
                
            // Create a string stream to parse the date string
            std::istringstream date_ss(DateStr);
                
            // Parse the date string using std::get_time
            date_ss >> std::get_time(&date_time, "%Y/%m/%d");
                
            // Check if parsing was successful
            if (date_ss.fail())
            {
                return false;
            }
                
            // Convert the parsed date to a time_t value
            date_time_t = mktime(&date_time);
                
            // Output the parsed date using std::asctime
            OutDate = date_time_t;

            return true;
        }

        bool parse_double(std::string DoubleStr, double& OutDouble)
        {
            try
            {
                OutDouble = std::stof(DoubleStr);
            }
            catch (std::invalid_argument const& ex)
            {
                return false;
            }
            
            return true;
        }
	
		bool prepare_stmt_column(sql::PreparedStatement& prep_stmt, EWeatherTableCols column_index, std::string column_value)
		{
			boost::trim(column_value);

            switch (column_index)
            {
                case EWeatherTableCols::DATE:
                    {
                        std::time_t date;
                        if (techtest::parser::parse_date(column_value, date))
                        {
                            char buff[20];
                            strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&date));
                            prep_stmt.setDateTime(column_index, sql::SQLString(buff));
                        }
                        else
                        {
							return false;
                            CROW_LOG_ERROR << " Parsing error on : " << column_value <<  " Date could not be parsed and it is a NOT NULL column";
                        }
                        break;
                    }
                case EWeatherTableCols::CITY:
                    {
                        std::string city = column_value; 
                        prep_stmt.setString(column_index, city);
                        break;
                    }
                case EWeatherTableCols::TEMP_MAX:
                case EWeatherTableCols::TEMP_MIN:
                case EWeatherTableCols::PRECIPITATION:
                case EWeatherTableCols::CLOUDINESS:
                    {
                        techtest::parser::replace(column_value, ",", ".");
                        double double_value;
                        if (techtest::parser::parse_double(column_value, double_value))
                        {
                            prep_stmt.setDouble(column_index, double_value);
                        }
                        else
                        {
                            prep_stmt.setNull(column_index,0);
                        }
                        break;
                    }
                default:
                    break;
            }
	
			return true;
		}
		
		bool prepare_stmt_for_line(sql::PreparedStatement& prep_stmt, std::string file_line)
		{
            EWeatherTableCols column_index = EWeatherTableCols::DATE;
            
            boost::trim(file_line);
            CROW_LOG_INFO << " Value: " << file_line;

            std::stringstream line_ss(file_line);
			
            std::string column;
            bool parse_succesfull = true;
            while (getline(line_ss, column, ';') && parse_succesfull && column_index != EWeatherTableCols::MAX)
            {
				parse_succesfull &= prepare_stmt_column(prep_stmt, column_index, column);
                column_index = static_cast<EWeatherTableCols>(static_cast<int>(column_index + 1));
            }

			return parse_succesfull;
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
};

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

    sql::mysql::MySQL_Driver* driver;
    sql::Connection* con;

    driver = sql::mysql::get_mysql_driver_instance();
    
    con = driver->connect("mysql:3306", "root", "pass");
    con->setSchema("weather_db");

    CROW_ROUTE(app, "/health")
    .methods("GET"_method)([]() {
        crow::json::wvalue health_status;
        health_status["status"] = "ok";
        return crow::response{200, health_status};
    });

    CROW_ROUTE(app, "/cities")
    .methods("GET"_method)([&con]() {
        crow::json::wvalue response;
        std::vector<std::string> cities;

        sql::ResultSet *res;
        sql::Statement *stmt;

        stmt = con->createStatement();
        try
        {        
            res = stmt->executeQuery("SELECT DISTINCT city FROM weather;");
            while (res->next()) {
                cities.push_back(res->getString(1));
            }
        }
        catch (std::exception const & ex)
        {
            crow::json::wvalue error_response;
            int response_code = 500; 
            error_response["code"] = response_code;
            error_response["message"] = "Internal server error";
            return crow::response{response_code, error_response};
        }

        delete res;
        delete stmt;
        response["cities"] = cities;
        return crow::response{200, response};
    });

    CROW_ROUTE(app, "/weather")
    .methods("GET"_method)([&con](const crow::request& req) {
        crow::json::wvalue response;

    	techtest::req_params_get_weather req_params(req);
		if (!req_params.is_valid)
		{
		    int response_code = 422; 
            response["code"] = response_code;
            response["message"] = req_params.error_response_str;
            return crow::response{response_code, response};
		}
        int offset = (req_params.page - 1) * req_params.limit;
        
        sql::ResultSet* res;
        sql::PreparedStatement* prep_stmt;

        std::string query = "SELECT date, city, temp_max, temp_min, precipitation, cloudiness FROM weather WHERE city LIKE ? AND date >= ? AND date <= ? ORDER BY date ASC LIMIT ? OFFSET ?;";
        prep_stmt = con->prepareStatement(query);
        prep_stmt->setString(1, req_params.city);
        prep_stmt->setDateTime(2, sql::SQLString(req_params.from));
        prep_stmt->setDateTime(3, sql::SQLString(req_params.to));
        prep_stmt->setInt(4, req_params.limit);
        prep_stmt->setInt(5, offset);

        res = prep_stmt->executeQuery();

		std::vector<crow::json::wvalue> days;
        while (res->next()) {
            crow::json::wvalue null_value(nullptr);
            crow::json::wvalue day;
            day["date"] = res->getString(1);
            day["city"] = res->getString(2);
            day["temp_max"] = !res->isNull(3) ? (float)res->getDouble(3) : null_value;
            day["temp_min"] = !res->isNull(4) ? (float)res->getDouble(4) : null_value;
            day["precipitation"] = !res->isNull(5) ? (float)res->getDouble(5) : null_value;
            day["cloudiness"] = !res->isNull(6) ? (float)res->getDouble(6) : null_value;
            days.push_back(day);
        }

        delete res;
        delete prep_stmt;

        response["days"] = std::move(days);
        return crow::response{200, response};
    });

    CROW_ROUTE(app, "/ingest/csv")
    .methods("POST"_method)([&con](const crow::request& req) {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

        crow::multipart::message file_message(req);

        int successful_insert = 0;
        int rejected_insert = 0;
        std::string file_checksum = "";

        for (const auto& part : file_message.part_map)
        {
            const auto& part_name = part.first;
            const auto& part_value = part.second;
            CROW_LOG_INFO << "Part: " << part_name;
         	if ("file" == part_name)
            {
                std::istringstream file_ss(part_value.body);
                file_checksum = techtest::checksum::sha256(file_ss.str());
                
                sql::PreparedStatement* prep_stmt;
                prep_stmt = con->prepareStatement("INSERT INTO weather (date, city, temp_max, temp_min, precipitation, cloudiness) VALUES (?, ?, ?, ?, ?, ?)");
                
                for (std::string file_line; std::getline(file_ss, file_line); )
                {
                    if (!techtest::parser::prepare_stmt_for_line(*prep_stmt, file_line))
                    {
                        rejected_insert++;
                        CROW_LOG_ERROR << " Row rejected due parsing error: " << file_line <<  "";
                        continue;
                    }

                    try
                    {
                        prep_stmt->execute();
                        successful_insert++; 
                    }
                    catch (std::exception const & ex)
                    {
                        CROW_LOG_ERROR << " Row rejected on insert try: " << file_line <<  " " << ex.what();
                        rejected_insert++; 
                    }
                }

                delete prep_stmt;         
            }
        }
   
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        crow::json::wvalue response;
        response["rows_inserted"] = successful_insert;
        response["rows_rejected"] = rejected_insert;
        response["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        std::ostringstream file_checksum_os;
        file_checksum_os << "sha256:" << file_checksum;
        response["file_checksum"] = file_checksum_os.str();
        return crow::response{200, response};
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    delete con;
    delete driver;
    return 0;
}