//// Includes
// Class
#include "utils.h"

// C++
#include <memory>
#include <sstream>
#include <iomanip>
#include <iostream>

// CROW
#include "crow.h"

// HIREDIS
#include <hiredis/hiredis.h>

// cURL
#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp>

// BOOST
#include <boost/algorithm/string.hpp>

// OpenSSL
#include <openssl/sha.h>

////////////////////////////////////////////////////////////////////
// techtest::parser
////////////////////////////////////////////////////////////////////

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

std::string techtest::parser::safe_strftime(const char *fmt, const std::tm *t)
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

////////////////////////////////////////////////////////////////////
// techtest::units
////////////////////////////////////////////////////////////////////

double techtest::units::c_to_f(double c)
{   
    // using the conversion formula
    return ((c * 9.0 / 5.0) + 32.0);
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
    
	for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
	{
		ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>( hash[i] );
	}

	return ss.str();
}

////////////////////////////////////////////////////////////////////
// techtest::service_a
////////////////////////////////////////////////////////////////////

techtest::service_a::service_a_query_params::service_a_query_params(std::string in_city, std::string in_from, std::string in_to, int in_page, int in_limit) : 
	city(in_city),
	from(in_from),
	to(in_to),
	page(in_page),
	limit(in_limit) 
{} 

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////

std::string techtest::service_a::do_service_a_query(service_a_query_params query_params, redisContext& redis_context)
{
	std::ostringstream url;
	std::ostringstream os;
	redisReply* reply;

	url << "service-a:8080/weather?city=" << query_params.city << "&from=" << query_params.from << "&to=" << query_params.to << "&page=" << query_params.page << "&limit=" << query_params.limit;
	std::string url_hash = checksum::sha256(url.str());

	std::string service_a_response;

	// Retrieve the value from Redis
	reply = (redisReply*)redisCommand(&redis_context, "GET %s", url_hash.c_str());
	CROW_LOG_INFO << "Try get cached Service A URL query: \"GET " << url_hash << "\"";
	if (reply->type == REDIS_REPLY_STRING)
	{
		service_a_response = reply->str;
		CROW_LOG_INFO << "Stored result in Redis: " << reply->str;

	}
	else
	{ 
		// RAII cleanup
		curlpp::Cleanup myCleanup;

		os << curlpp::options::Url(url.str());
		service_a_response = os.str();
		CROW_LOG_INFO << "No cache available. Query result: " << os.str();
		
		freeReplyObject(reply);

		// Set a value in Redis
		auto response_json_str = crow::json::load(service_a_response);
		crow::json::rvalue response_json(response_json_str);
		std::ostringstream response_json_ss;
		response_json_ss << response_json;
		reply = (redisReply*)redisCommand(&redis_context, "SET %s %s", url_hash.c_str(), service_a_response.c_str());
		CROW_LOG_INFO << "Caching Query result \"SET " << url_hash << " " << response_json_ss.str() << "\" ...";

		freeReplyObject(reply);
		reply = (redisReply*)redisCommand(&redis_context, "EXPIRE %s %d", url_hash.c_str(), techtest::CACHE_EXPIRE_TIME);
	}

	freeReplyObject(reply);

	return service_a_response;
}

////////////////////////////////////////////////////////////////////
// techtest::req_params_get_weather
////////////////////////////////////////////////////////////////////

techtest::req_params_get_weather::req_params_get_weather(const crow::request& req, std::string in_city)
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

////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////