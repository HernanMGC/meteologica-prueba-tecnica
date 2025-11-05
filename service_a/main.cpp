// Includes
#include "crow.h"
#include "mysql_driver.h"
#include <cppconn/prepared_statement.h>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <openssl/md5.h>
#include <fstream>
#include <iomanip>
#include <iostream>

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
    namespace parser
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
    }

    namespace checksum
    {
        // Function to print the MD5 hash in hexadecimal format 
        std::string get_MD5(unsigned char *md, long size = MD5_DIGEST_LENGTH){
            std::ostringstream md5;
    
            for (int i = 0; i < size; i++){
                md5 << std::hex << std::setw(2) << std::setfill('0') << (int)md[i];
            }
            return md5.str();
        }

        // Function to compute and print MD5 hash of a given string
        std::string get_md5_from_string(const std::string &str){
            unsigned char result[MD5_DIGEST_LENGTH];
            MD5((unsigned char *)str.c_str(), str.length(), result);

            return get_MD5(result);
        }

        // Function to compute and print MD5 hash of a file
        std::string get_md5_from_file(const std::string &filePath){
            std::ifstream file(filePath, std::ios::in | std::ios::binary | std::ios::ate);

            if (!file.is_open()){
                return "";
            }

            // Get file size
            long fileSize = file.tellg();

            // Allocate memory to hold the entire file
            char *memBlock = new char[fileSize];

            // Read the file into memory
            file.seekg(0, std::ios::beg);
            file.read(memBlock, fileSize);
            file.close();

            // Compute the MD5 hash of the file content
            unsigned char result[MD5_DIGEST_LENGTH];
            MD5((unsigned char *)memBlock, fileSize, result);

            // Clean up
            delete[] memBlock;

            return get_MD5(result);
        }
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

    CROW_ROUTE(app, "/cities")
    .methods("GET"_method)([&con]() {
        crow::json::wvalue response;
        std::vector<std::string> cities;

        sql::ResultSet *res;
        sql::Statement *stmt;

        stmt = con->createStatement();
        res = stmt->executeQuery("SELECT DISTINCT city FROM weather"
        );
        while (res->next()) {
            cities.push_back(res->getString(1));
        }
        delete res;
        delete stmt;
        response["cities"] = cities;
        return crow::response{200, response};
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
                file_checksum = techtest::checksum::get_md5_from_file(outfile_name);
            }
            else if ("file" == part_name)
            {
                std::istringstream file_ss(part_value.body);
                file_checksum = techtest::checksum::get_md5_from_string(file_ss.str());
                
                sql::PreparedStatement* prep_stmt;
                prep_stmt = con->prepareStatement("INSERT INTO weather (date, city, temp_max, temp_min, precipitation, cloudiness) VALUES (?, ?, ?, ?, ?, ?)");
                
                for (std::string file_line; std::getline(file_ss, file_line); )
                {
                    int i = 0;
                    
                    boost::trim(file_line);
                    CROW_LOG_INFO << " Value: " << file_line;

                    std::stringstream line_ss(file_line);
                    std::string column;
                    bool parsing_error = false;
                    while (getline(line_ss, column, ';') && !parsing_error)
                    {
                        boost::trim(column);

                        switch (i)
                        {
                            case EWeatherTableCols::DATE:
                                {
                                    std::time_t date;
                                    parsing_error |= !techtest::parser::parse_date(column, date);
                                    if (!parsing_error)
                                    {
                                        char buff[20];
                                        strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&date));
                                        prep_stmt->setDateTime(i+1, sql::SQLString(buff));
                                    }
                                    else
                                    {
                                        CROW_LOG_ERROR << " Parsing error on : " << file_line <<  " Date could not be parsed and it is a NOT NULL column";
                                    }
                                    break;
                                }
                            case EWeatherTableCols::CITY:
                                {
                                    std::string city = column; 
                                    prep_stmt->setString(i+1, city);
                                    break;
                                }
                            case EWeatherTableCols::TEMP_MAX:
                            case EWeatherTableCols::TEMP_MIN:
                            case EWeatherTableCols::PRECIPITATION:
                            case EWeatherTableCols::CLOUDINESS:
                                {
                                    double double_value;
                                    if (techtest::parser::parse_double(column, double_value))
                                    {
                                        prep_stmt->setDouble(i+1, double_value);
                                    }
                                    else
                                    {
                                        prep_stmt->setNull(i+1,0);
                                    }
                                    break;
                                }
                            default:
                                break;
                        }
                        i++;
                    }

                    if (parsing_error)
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
        std::ostringstream elapsed_time;
        elapsed_time << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[µs]";;
        response["elapsed_ms"] = elapsed_time.str();
        response["file_checksum"] = file_checksum;
        return crow::response{201, response};
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    delete con;
    return 0;
}