#pragma once

#include <boost/beast/http.hpp>
#include <string>

namespace tcdn
{
	boost::beast::http::response<boost::beast::http::vector_body<char>> http_download(const std::string& host, const std::string& port, const std::string& target);
}