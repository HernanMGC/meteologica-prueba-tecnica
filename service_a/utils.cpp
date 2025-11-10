//// Includes
// Class
#include "utils.h"

// CROW
#include "crow.h"

// Boost
#include <boost/algorithm/string.hpp>

// CPPCONN
#include <cppconn/prepared_statement.h>

// OpenSSL
#include <openssl/sha.h>

////////////////////////////////////////////////////////////////////
// techtest::req_params_get_weather
////////////////////////////////////////////////////////////////////

techtest::req_params_get_weather::req_params_get_weather(const crow::request& req)
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

////////////////////////////////////////////////////////////////////
// techtest::parser
////////////////////////////////////////////////////////////////////

namespace techtest
{
    namespace parser
    {}}
bool techtest::parser::replace(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    if(start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////

bool techtest::parser::parse_date(std::string DateStr, std::time_t& OutDate)
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

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////

bool techtest::parser::parse_double(std::string DoubleStr, double& OutDouble)
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

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////

bool techtest::parser::prepare_stmt_column(sql::PreparedStatement& prep_stmt, EWeatherTableCols column_index, std::string column_value)
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

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////

bool techtest::parser::prepare_stmt_for_line(sql::PreparedStatement& prep_stmt, std::string file_line)
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

////////////////////////////////////////////////////////////////////
// techtest::checksum
////////////////////////////////////////////////////////////////////

std::string techtest::checksum::sha256(const std::string str)
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

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////
