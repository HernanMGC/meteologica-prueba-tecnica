#include "crow.h"
#include <iostream>
#include <sqlite3.h>
#include <string>
#include <sstream>
#include <ctime>
#include <vector>

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
    std::string date;
};

int main() {
    crow::SimpleApp app;

    sqlite3* db;
    int rc = sqlite3_open("weather.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    char* zErrMsg = nullptr;
    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS weather ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "date DATE NOT NULL"
        //", city TEXT NOT NULL"
        //", temp_max DECIMAL(10, 5) NOT NULL"
        //", temp_min DECIMAL(10, 5) NOT NULL"
        //", precipitation DECIMAL(10, 5) NOT NULL"
        //", cloudiness DECIMAL(10, 5) NOT NULL"
        "); ",
        nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg);
        sqlite3_close(db);
        return 1;
    }

    CROW_ROUTE(app, "/health")
    .methods("GET"_method)([]() {
        crow::json::wvalue health_status;
        health_status["status"] = "ok";
        return crow::response{200, health_status};
    });

    CROW_ROUTE(app, "/weather")
    .methods("GET"_method)([&db]() {
        std::vector<crow::json::wvalue> weather_lines;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT "
            "id"
            ", date"
            // ", city"
            // ", temp_max"
            // ", temp_min"
            // ", precipitation"
            // ", cloudiness"
            " FROM weather", -1, &stmt, nullptr) != SQLITE_OK) {
            return crow::response{500, "{\"error\": \"Database error\"}"};
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue weather_line;
            weather_line["id"] = sqlite3_column_int(stmt, 0);
            weather_line["date"] = (const char*)sqlite3_column_text(stmt, 1);
            // weather_line["city"] = (const char*)sqlite3_column_text(stmt, 2);
            // weather_line["temp_max"] = sqlite3_column_double(stmt, 3);
            // weather_line["temp_min"] = sqlite3_column_double(stmt, 4);
            // weather_line["precipitation"] = sqlite3_column_double(stmt, 5);
            // weather_line["cloudiness"] = sqlite3_column_double(stmt, 6);
            weather_lines.push_back(weather_line);
        }
        sqlite3_finalize(stmt);

        return crow::response{200, crow::json::wvalue{{"weather_lines", weather_lines}}};
    });

    CROW_ROUTE(app, "/weather")
    .methods("POST"_method)([&db](const crow::request& req) {
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
                
                for (std::string file_line; std::getline(file_ss, file_line); )
                {
                    CROW_LOG_DEBUG << " Value: " << file_line << '\n';

                    std::vector<std::string> weather_line;
                    std::stringstream line_ss(file_line);
                    
                    SWeatherLine weather_line_s;
                    int i = 0;
                    while (line_ss.good())
                    {
                        std::string column;
                        getline(line_ss, column, ';');
                        weather_line.push_back(column); 
                       
                        tm date_time = {};
                        std::time_t date_time_t;

                        // Create a string stream to parse the date string
                        std::istringstream date_ss(weather_line[i]);

                        switch (i)
                        {
                            case EWeatherTableCols::DATE:

                                // Parse the date string using std::get_time
                                date_ss >> std::get_time(&date_time, "%Y/%m/%d");

                                // Check if parsing was successful
                                if (date_ss.fail())
                                {
                                    CROW_LOG_ERROR << "Date parsing failed!" << '\n';
                                }

                                // Convert the parsed date to a time_t value
                                date_time_t = mktime(&date_time);

                                // Output the parsed date using std::asctime
                                CROW_LOG_INFO << " Value[" << i << "]: " << asctime(localtime(&date_time_t)) << '\n';
                                weather_line_s.date = asctime(localtime(&date_time_t));
                                break;
                            default:
                                break;
                        }    

                        i++;
                    }

                    
                    sqlite3_stmt* stmt;
                    const char* sql = "INSERT INTO weather (date) VALUES (?)";
                    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
                        return crow::response{500, "{\"error\": \"Database error\"}"};
                    }
                    
                    sqlite3_bind_text(stmt, 1, weather_line_s.date.c_str(), -1, SQLITE_STATIC);
                
                    if (sqlite3_step(stmt) != SQLITE_DONE) {
                        sqlite3_finalize(stmt);
                        return crow::response{500, "{\"error\": \"Failed to insert todo\"}"};
                    }

                    sqlite3_finalize(stmt);
                }                
            }
        }

        // auto json = crow::json::load(req.body);
        // if (!json || !json.has("title")) {
        //     return crow::response{400, "{\"error\": \"Title is required\"}"};
        // }
    //
    //    std::string title = json["title"].s();
    //    int completed = 0;
    //
    //    if (json.has("completed")) {
    //        try {
    //            completed = json["completed"].b() ? 1 : 0;
    //        } catch (const std::runtime_error& e) {
    //            if (json["completed"].t() == crow::json::type::Number) {
    //                completed = json["completed"].i();
    //            } else {
    //                return crow::response{400, "{\"error\": \"Completed must be a boolean or a number\"}"};
    //            }
    //        }
    //    }
    //
    //    sqlite3_stmt* stmt;
    //    const char* sql = "INSERT INTO todos (title, completed) VALUES (?, ?)";
    //    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    //        return crow::response{500, "{\"error\": \"Database error\"}"};
    //    }
    //
    //    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
    //    sqlite3_bind_int(stmt, 2, completed);
    //
    //    if (sqlite3_step(stmt) != SQLITE_DONE) {
    //        sqlite3_finalize(stmt);
    //        return crow::response{500, "{\"error\": \"Failed to insert todo\"}"};
    //    }
    //
    //    sqlite3_finalize(stmt);
    //    return crow::response{201, "{\"message\": \"Todo added successfully\"}"};
        return crow::response{201, "{\"message\": \"THIS IS A LIE. Weather added successfully\"}"};
    });
    
    CROW_ROUTE(app, "/weather/<int>")
    .methods("PUT"_method)([&db](const crow::request& req, int id) {
    //    auto json = crow::json::load(req.body);
    //    if (!json || !json.has("completed")) {
    //        return crow::response{400, "{\"error\": \"Completed status is required\"}"};
    //    }
    //
    //    int completed = 0;
    //
    //    if (json.has("completed")) {
    //        try {
    //            completed = json["completed"].b() ? 1 : 0;
    //        } catch (const std::runtime_error& e) {
    //            if (json["completed"].t() == crow::json::type::Number) {
    //                completed = json["completed"].i();
    //            } else {
    //                return crow::response{400, "{\"error\": \"Completed must be a boolean or a number\"}"};
    //            }
    //        }
    //    }
    //
    //    sqlite3_stmt* stmt;
    //    if (sqlite3_prepare_v2(db, "UPDATE todos SET completed = ? WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
    //        return crow::response{500, "{\"error\": \"Database error\"}"};
    //    }
    //
    //    sqlite3_bind_int(stmt, 1, completed);
    //    sqlite3_bind_int(stmt, 2, id);
    //
    //    if (sqlite3_step(stmt) != SQLITE_DONE) {
    //        sqlite3_finalize(stmt);
    //        return crow::response{500, "{\"error\": \"Failed to update todo\"}"};
    //    }
    //
    //    sqlite3_finalize(stmt);
    //    return crow::response{200, "{\"message\": \"Todo updated successfully\"}"};
        return crow::response{200, "{\"message\": \"THIS IS A LIE. Weather line updated successfully\"}"};
    });

    CROW_ROUTE(app, "/weather/<int>")
    .methods("DELETE"_method)([&db](int id) {
    //    sqlite3_stmt* stmt;
    //    if (sqlite3_prepare_v2(db, "DELETE FROM todos WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
    //        return crow::response{500, "{\"error\": \"Database error\"}"};
    //    }
    //
    //    sqlite3_bind_int(stmt, 1, id);
    //
    //    if (sqlite3_step(stmt) != SQLITE_DONE) {
    //        sqlite3_finalize(stmt);
    //        return crow::response{500, "{\"error\": \"Failed to delete todo\"}"};
    //    }
    //
    //    sqlite3_finalize(stmt);
    //    return crow::response{200, "{\"message\": \"Todo deleted successfully\"}"};
        return crow::response{200, "{\"message\": \"THIS IS A LIE. Weather line deleted successfully\"}"};
    });

    CROW_ROUTE(app, "/weather/<int>")
    .methods("GET"_method)([&db](int id) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT "
            "id, "
            "date, "
            "city, "
            "temp_max, "
            "temp_min, "
            "precipitation, "
            "cloudiness FROM weather WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
            return crow::response{500, "{\"error\": \"Database error\"}"};
        }

        sqlite3_bind_int(stmt, 1, id);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue weather_line;
            weather_line["id"] = sqlite3_column_int(stmt, 0);
            weather_line["date"] = (const char*)sqlite3_column_text(stmt, 1);
            weather_line["city"] = (const char*)sqlite3_column_text(stmt, 2);
            weather_line["temp_max"] = sqlite3_column_double(stmt, 3);
            weather_line["temp_min"] = sqlite3_column_double(stmt, 4);
            weather_line["precipitation"] = sqlite3_column_double(stmt, 5);
            weather_line["cloudiness"] = sqlite3_column_double(stmt, 6);
            sqlite3_finalize(stmt);
            return crow::response{200, weather_line};
        } else {
            sqlite3_finalize(stmt);
            return crow::response{404, "{\"error\": \"Weather line not found\"}"};
        }
    });

    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();

    sqlite3_close(db);
    return 0;
}