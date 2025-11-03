// Includes
#include "crow.h"
#include "mysql_driver.h"
#include <cppconn/prepared_statement.h>

enum EWeatherTableCols
{
    DATE = 0,
    CITY,
    TEMP_MAX,
    TEMP_MIN,
    PRECIPITATION,
    CLOUDINESS
};

struct SWeatherLine
{
    std::time_t date;
    std::string city = "";
    double temp_max = 0.d;
    double temp_min = 0.d;
    double precipitation = 0.d;
    double cloudiness = 0.d;
};

namespace techtest
{
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
            [[maybe_unused]] OutDouble = std::stof(DoubleStr);
        }
        catch (std::invalid_argument const& ex)
        {
            return false;
        }
        
        return true;
    }
};

int main() {
    crow::SimpleApp app;

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

    CROW_ROUTE(app, "/weather")
    .methods("GET"_method)([&con]() {
        std::vector<crow::json::wvalue> weather_lines;
        
        sql::ResultSet *res;
        sql::Statement *stmt;

        stmt = con->createStatement();
        res = stmt->executeQuery("SELECT "
            "id"
            ", date"
            ", city"
            ", temp_max"
            ", temp_min"
            ", precipitation"
            ", cloudiness"
            " FROM weather"
        );
        while (res->next()) {
            crow::json::wvalue weather_line;
            weather_line["id"] = res->getInt(1);
            weather_line["date"] = res->getString(2);
            weather_line["city"] = res->getString(3);
            weather_line["temp_max"] = (float)res->getDouble(4);
            weather_line["temp_min"] = (float)res->getDouble(5);
            weather_line["precipitation"] = (float)res->getDouble(6);
            weather_line["cloudiness"] = (float)res->getDouble(7);
            weather_lines.push_back(weather_line);
        }
        delete res;
        delete stmt;

        return crow::response{200, crow::json::wvalue{{"weather_lines", weather_lines}}};
    });

    CROW_ROUTE(app, "/weather")
    .methods("POST"_method)([&con](const crow::request& req) {
        crow::multipart::message file_message(req);
        for (const auto& part : file_message.part_map)
        {
            const auto& part_name = part.first;
            const auto& part_value = part.second;
            CROW_LOG_DEBUG << "Part: " << part_name;
            if ("InputFile" == part_name)
            {
                // Extract the file name
                auto headers_it = part_value.headers.find("Content-Disposition");
                if (headers_it == part_value.headers.end())
                {
                    CROW_LOG_ERROR << "No Content-Disposition found";
                    return crow::response(400);
                }
                auto params_it = headers_it->second.params.find("filename");
                if (params_it == headers_it->second.params.end())
                {
                    CROW_LOG_ERROR << "Part with name \"InputFile\" should have a file";
                    return crow::response(400);
                }
                const std::string outfile_name = params_it->second;
        
                for (const auto& part_header : part_value.headers)
                {
                    const auto& part_header_name = part_header.first;
                    const auto& part_header_val = part_header.second;
                    CROW_LOG_DEBUG << "Header: " << part_header_name << '=' << part_header_val.value;
                    for (const auto& param : part_header_val.params)
                    {
                        const auto& param_key = param.first;
                        const auto& param_val = param.second;
                        CROW_LOG_DEBUG << " Param: " << param_key << ',' << param_val;
                    }
                }
        
                // Create a new file with the extracted file name and write file contents to it
                std::ofstream out_file(outfile_name);
                if (!out_file)
                {
                    CROW_LOG_ERROR << " Write to file failed\n";
                    continue;
                }
                out_file << part_value.body;
                out_file.close();
                CROW_LOG_INFO << " Contents written to " << outfile_name << '\n';
            }
            else
            {
                std::istringstream file_ss(part_value.body);

                sql::PreparedStatement* prep_stmt;
                prep_stmt = con->prepareStatement("INSERT INTO weather (date, city, temp_max, temp_min, precipitation, cloudiness) VALUES (?, ?, ?, ?, ?, ?)");
                
                for (std::string file_line; std::getline(file_ss, file_line); )
                {
                    int i = 0;

                    CROW_LOG_INFO << " Value: " << file_line;

                    SWeatherLine weather_line_s;
                    std::stringstream line_ss(file_line);
                    std::string column;
                    bool parsing_error = false;
                    while (getline(line_ss, column, ';') && !parsing_error)
                    {
                        CROW_LOG_INFO << " COL: " << column;

                        switch (i)
                        {
                            case EWeatherTableCols::DATE:
                                parsing_error |= !techtest::parse_date(column, weather_line_s.date);
                                //{
                                //    tm date_time = {};
                                //    std::time_t date_time_t;
                                //
                                //    // Create a string stream to parse the date string
                                //    std::istringstream date_ss(column);
                                //
                                //    // Parse the date string using std::get_time
                                //    date_ss >> std::get_time(&date_time, "%Y/%m/%d");
                                //
                                //    // Check if parsing was successful
                                //    if (date_ss.fail())
                                //    {
                                //        parsing_error = true;
                                //        CROW_LOG_ERROR << "Date parsing failed!" << '\n';
                                //    }
                                //
                                //    // Convert the parsed date to a time_t value
                                //    date_time_t = mktime(&date_time);
                                //
                                //    // Output the parsed date using std::asctime
                                //    weather_line_s.date = date_time_t;
                                //}                        
                                break;

                            case EWeatherTableCols::CITY:
                                weather_line_s.city = column; 
                                break;
                            case EWeatherTableCols::TEMP_MAX:
                                parsing_error |= !techtest::parse_double(column, weather_line_s.temp_max);
                                //try
                                //{
                                //    [[maybe_unused]] weather_line_s.temp_max = std::stof(column);
                                //}
                                //catch (std::invalid_argument const& ex)
                                //{
                                //    parsing_error = true;
                                //    CROW_LOG_ERROR << "Float parsing failed!" << '\n';
                                //}
                                break;
                            case EWeatherTableCols::TEMP_MIN:
                                parsing_error |= !techtest::parse_double(column, weather_line_s.temp_min);
                                //try
                                //{
                                //    [[maybe_unused]] weather_line_s.temp_min = std::stof(column);
                                //}
                                //catch (std::invalid_argument const& ex)
                                //{
                                //    parsing_error = true;
                                //    CROW_LOG_ERROR << "Float parsing failed!" << '\n';
                                //}
                                break;
                            case EWeatherTableCols::PRECIPITATION:
                                parsing_error |= !techtest::parse_double(column, weather_line_s.precipitation);
                                //try
                                //{
                                //    [[maybe_unused]] weather_line_s.precipitation = std::stof(column);
                                //}
                                //catch (std::invalid_argument const& ex)
                                //{
                                //    parsing_error = true;
                                //    CROW_LOG_ERROR << "Float parsing failed!" << '\n';
                                //}
                                break;
                            case EWeatherTableCols::CLOUDINESS:
                                parsing_error |= !techtest::parse_double(column, weather_line_s.cloudiness);
                                //try
                                //{
                                //    [[maybe_unused]] weather_line_s.cloudiness = std::stof(column);
                                //}
                                //catch (std::invalid_argument const& ex)
                                //{
                                //    parsing_error = true;
                                //    CROW_LOG_ERROR << "Float parsing failed!" << '\n';
                                //}
                                break;
                            default:
                                break;
                        }
                        i++;
                    }

                    if (parsing_error) { continue; }
                    char buff[20];
                    strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&weather_line_s.date));
                    CROW_LOG_INFO << " Value[" << i << "]: " << buff << '\n';
                    prep_stmt->setDateTime(1, sql::SQLString(buff));
                    prep_stmt->setString(2, weather_line_s.city);
                    prep_stmt->setDouble(3,weather_line_s.temp_max);
                    prep_stmt->setDouble(4,weather_line_s.temp_min);
                    prep_stmt->setDouble(5,weather_line_s.precipitation);
                    prep_stmt->setDouble(6,weather_line_s.cloudiness);
                    prep_stmt->execute();
                }

                delete prep_stmt;         
            }
        }
   
        return crow::response{201, "{\"message\": \"THIS IS A LIE. Weather added successfully\"}"};
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    delete con;
    return 0;
}