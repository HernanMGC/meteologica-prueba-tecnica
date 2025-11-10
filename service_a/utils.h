// Includes
#include <chrono>
#include <string>
#include <vector>

// ForwardDeclarations
// CROW
namespace crow
{
	class request;
}

// CPPCONN
namespace sql
{
	class PreparedStatement;
}

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
		    req_params_get_weather(const crow::request& req);
	};

    namespace parser
    {
        bool replace(std::string& str, const std::string& from, const std::string& to);

        bool parse_date(std::string DateStr, std::time_t& OutDate);

        bool parse_double(std::string DoubleStr, double& OutDouble);
	
		bool prepare_stmt_column(sql::PreparedStatement& prep_stmt, EWeatherTableCols column_index, std::string column_value);
		
		bool prepare_stmt_for_line(sql::PreparedStatement& prep_stmt, std::string file_line);
    }

    namespace checksum
    {
		std::string sha256(const std::string str);
    }

    namespace parser
    {
        bool replace(std::string& str, const std::string& from, const std::string& to);

        bool parse_date(std::string DateStr, std::time_t& OutDate);

        bool parse_double(std::string DoubleStr, double& OutDouble);

		bool prepare_stmt_column(sql::PreparedStatement& prep_stmt, EWeatherTableCols column_index, std::string column_value);
		
		bool prepare_stmt_for_line(sql::PreparedStatement& prep_stmt, std::string file_line);
    }

    namespace checksum
    {
		std::string sha256(const std::string str);
    }
};
