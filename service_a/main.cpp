//// Includes
// TECHTEST
#include "utils.h"

// CROW
#include "crow.h"
#include "crow/middlewares/cors.h"

// CPPCONN
#include "mysql_driver.h"
#include <cppconn/prepared_statement.h>

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