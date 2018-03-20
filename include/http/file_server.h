#pragma once

#include <http/server.h>

#include <algorithm>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using tcp = asio::ip::tcp;
namespace beast = boost::beast;

namespace http
{
	// Return a reasonable mime type based on the extension of a file.
	inline boost::beast::string_view mime_type(boost::beast::string_view path)
	{
		using boost::beast::iequals;
		auto const ext = [&path]
		{
			auto const pos = path.rfind(".");

			if (pos == boost::beast::string_view::npos ||
				pos == path.size() - 1) // is last character
				return boost::beast::string_view{};

			return path.substr(pos + 1);
		}();

		if (iequals(ext, "htm"))  return "text/html";
		if (iequals(ext, "html")) return "text/html";
		if (iequals(ext, "php"))  return "text/html";
		if (iequals(ext, "css"))  return "text/css";
		if (iequals(ext, "txt"))  return "text/plain";
		if (iequals(ext, "js"))   return "application/javascript";
		if (iequals(ext, "json")) return "application/json";
		if (iequals(ext, "xml"))  return "application/xml";
		if (iequals(ext, "swf"))  return "application/x-shockwave-flash";
		if (iequals(ext, "flv"))  return "video/x-flv";
		if (iequals(ext, "png"))  return "image/png";
		if (iequals(ext, "jpe"))  return "image/jpeg";
		if (iequals(ext, "jpeg")) return "image/jpeg";
		if (iequals(ext, "jpg"))  return "image/jpeg";
		if (iequals(ext, "gif"))  return "image/gif";
		if (iequals(ext, "bmp"))  return "image/bmp";
		if (iequals(ext, "ico"))  return "image/vnd.microsoft.icon";
		if (iequals(ext, "tiff")) return "image/tiff";
		if (iequals(ext, "tif"))  return "image/tiff";
		if (iequals(ext, "svg"))  return "image/svg+xml";
		if (iequals(ext, "svgz")) return "image/svg+xml";
		return "application/text";
	}

	// Append an HTTP rel-path to a local filesystem path.
	// The returned path is normalized for the platform.
	inline std::string path_cat(boost::beast::string_view base, boost::beast::string_view path)
	{
		if (base.empty())
			return path.to_string();

		std::string result = base.to_string();
#if BOOST_MSVC
		char constexpr path_separator = '\\';
		
		if (result.back() == path_separator)
			result.pop_back();

		result.append(path.data(), path.size());
		for (auto& c : result)
			if (c == '/')
				c = path_separator;
#else
		char constexpr path_separator = '/';
		if (result.back() == path_separator)
			result.pop_back();
		result.append(path.data(), path.size());
#endif
		return result;
	}

	// This function produces an HTTP response for the given
	// request. The type of the response object depends on the
	// contents of the request, so the interface requires the
	// caller to pass a generic lambda for receiving the response.
	template<
		typename Body, typename Allocator,
		typename RequestHandler>
	void handle_request(
		boost::beast::string_view doc_root,
		beast::http::request<Body, beast::http::basic_fields<Allocator>>&& request,
		std::shared_ptr<session<RequestHandler>> session)
	{
		// Returns a bad request response
		auto const bad_request = [&request](boost::beast::string_view why)
		{
			beast::http::response<beast::http::string_body> res{ beast::http::status::bad_request, request.version() };
			res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(beast::http::field::content_type, "text/html");
			res.keep_alive(request.keep_alive());
			res.body() = why.to_string();
			res.prepare_payload();
			return res;
		};

		// Returns a not found response
		auto const not_found = [&request](boost::beast::string_view target)
		{
			beast::http::response<beast::http::string_body> res{ beast::http::status::not_found, request.version() };
			res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(beast::http::field::content_type, "text/html");
			res.keep_alive(request.keep_alive());
			res.body() = "The resource '" + target.to_string() + "' was not found.";
			res.prepare_payload();
			return res;
		};

		// Returns a server error response
		auto const server_error = [&request](boost::beast::string_view what)
		{
			beast::http::response<beast::http::string_body> res{ beast::http::status::internal_server_error, request.version() };
			res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
			res.set(beast::http::field::content_type, "text/html");
			res.keep_alive(request.keep_alive());
			res.body() = "An error occurred: '" + what.to_string() + "'";
			res.prepare_payload();
			return res;
		};


		// Make sure we can handle the method
		if (request.method() != beast::http::verb::get &&
			request.method() != beast::http::verb::head)
			return session->respond(bad_request("Unknown HTTP-method"));

		// Request path must be absolute and not contain "..".
		if (request.target().empty() ||
			request.target()[0] != '/' ||
			request.target().find("..") != boost::beast::string_view::npos)
			return session->respond(bad_request("Illegal request-target"));

		// Build the path to the requested file
		std::string path = path_cat(doc_root, request.target());
		if (request.target().back() == '/')
			path.append("index.html");

		// Attempt to open the file
		boost::beast::error_code ec;
		beast::http::file_body::value_type body;
		body.open(path.c_str(), boost::beast::file_mode::scan, ec);

		// Handle the case where the file doesn't exist
		if (ec == boost::system::errc::no_such_file_or_directory)
			return session->respond(not_found(request.target()));

		// Handle an unknown error
		if (ec)
			return session->respond(server_error(ec.message()));

		// Respond to HEAD request
		if (request.method() == beast::http::verb::head)
		{
			beast::http::response<beast::http::empty_body> response{ beast::http::status::ok, request.version() };
			response.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
			response.set(beast::http::field::content_type, mime_type(path));
			response.content_length(body.size());
			response.keep_alive(request.keep_alive());

			return session->respond(std::move(response));
		}

		// Respond to GET request
		beast::http::response<beast::http::file_body> response{
			std::piecewise_construct,
			std::make_tuple(std::move(body)),
			std::make_tuple(beast::http::status::ok, request.version()) };
		response.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
		response.set(beast::http::field::content_type, mime_type(path));
		response.content_length(body.size());
		response.keep_alive(request.keep_alive());

		return session->respond(std::move(response));
	}


	// Accepts incoming connections and launches the sessions
	struct file_server : http::server
	{
		std::string const& doc_root;

		file_server(asio::io_context& context, uint16_t port, std::string const& doc_root) :
			server(context, port),
			doc_root(doc_root)
		{
		}

		// Start accepting incoming connections
		void run()
		{
			accept([this](auto& session, auto&& request)
			{
				handle_request(doc_root, std::move(request), session);
			});
		}
	};
}