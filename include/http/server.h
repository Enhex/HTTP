#pragma once

#include <hla/server.h>

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

// Return a reasonable mime type based on the extension of a file.
boost::beast::string_view
mime_type(boost::beast::string_view path)
{
	using boost::beast::iequals;
	auto const ext = [&path]
	{
		auto const pos = path.rfind(".");
		if (pos == boost::beast::string_view::npos)
			return boost::beast::string_view{};
		return path.substr(pos);
	}();
	if (iequals(ext, ".htm"))  return "text/html";
	if (iequals(ext, ".html")) return "text/html";
	if (iequals(ext, ".php"))  return "text/html";
	if (iequals(ext, ".css"))  return "text/css";
	if (iequals(ext, ".txt"))  return "text/plain";
	if (iequals(ext, ".js"))   return "application/javascript";
	if (iequals(ext, ".json")) return "application/json";
	if (iequals(ext, ".xml"))  return "application/xml";
	if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
	if (iequals(ext, ".flv"))  return "video/x-flv";
	if (iequals(ext, ".png"))  return "image/png";
	if (iequals(ext, ".jpe"))  return "image/jpeg";
	if (iequals(ext, ".jpeg")) return "image/jpeg";
	if (iequals(ext, ".jpg"))  return "image/jpeg";
	if (iequals(ext, ".gif"))  return "image/gif";
	if (iequals(ext, ".bmp"))  return "image/bmp";
	if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
	if (iequals(ext, ".tiff")) return "image/tiff";
	if (iequals(ext, ".tif"))  return "image/tiff";
	if (iequals(ext, ".svg"))  return "image/svg+xml";
	if (iequals(ext, ".svgz")) return "image/svg+xml";
	return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
	boost::beast::string_view base,
	boost::beast::string_view path)
{
	if (base.empty())
		return path.to_string();
	std::string result = base.to_string();
#if BOOST_MSVC
	char constexpr path_separator = '\\';
	if (result.back() == path_separator)
		result.resize(result.size() - 1);
	result.append(path.data(), path.size());
	for (auto& c : result)
		if (c == '/')
			c = path_separator;
#else
	char constexpr path_separator = '/';
	if (result.back() == path_separator)
		result.resize(result.size() - 1);
	result.append(path.data(), path.size());
#endif
	return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<
	class Body, class Allocator,
	class Send>
	void
	handle_request(
		boost::beast::string_view doc_root,
		beast::http::request<Body, beast::http::basic_fields<Allocator>>&& req,
		Send&& send)
{
	// Returns a bad request response
	auto const bad_request =
		[&req](boost::beast::string_view why)
	{
		beast::http::response<beast::http::string_body> res{ beast::http::status::bad_request, req.version() };
		res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(beast::http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = why.to_string();
		res.prepare_payload();
		return res;
	};

	// Returns a not found response
	auto const not_found =
		[&req](boost::beast::string_view target)
	{
		beast::http::response<beast::http::string_body> res{ beast::http::status::not_found, req.version() };
		res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(beast::http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "The resource '" + target.to_string() + "' was not found.";
		res.prepare_payload();
		return res;
	};

	// Returns a server error response
	auto const server_error =
		[&req](boost::beast::string_view what)
	{
		beast::http::response<beast::http::string_body> res{ beast::http::status::internal_server_error, req.version() };
		res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(beast::http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "An error occurred: '" + what.to_string() + "'";
		res.prepare_payload();
		return res;
	};

	// Make sure we can handle the method
	if (req.method() != beast::http::verb::get &&
		req.method() != beast::http::verb::head)
		return send(bad_request("Unknown HTTP-method"));

	// Request path must be absolute and not contain "..".
	if (req.target().empty() ||
		req.target()[0] != '/' ||
		req.target().find("..") != boost::beast::string_view::npos)
		return send(bad_request("Illegal request-target"));

	// Build the path to the requested file
	std::string path = path_cat(doc_root, req.target());
	if (req.target().back() == '/')
		path.append("index.html");

	// Attempt to open the file
	boost::beast::error_code ec;
	beast::http::file_body::value_type body;
	body.open(path.c_str(), boost::beast::file_mode::scan, ec);

	// Handle the case where the file doesn't exist
	if (ec == boost::system::errc::no_such_file_or_directory)
		return send(not_found(req.target()));

	// Handle an unknown error
	if (ec)
		return send(server_error(ec.message()));

	// Respond to HEAD request
	if (req.method() == beast::http::verb::head)
	{
		beast::http::response<beast::http::empty_body> res{ beast::http::status::ok, req.version() };
		res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(beast::http::field::content_type, mime_type(path));
		res.content_length(body.size());
		res.keep_alive(req.keep_alive());
		return send(std::move(res));
	}

	// Respond to GET request
	beast::http::response<beast::http::file_body> res{
		std::piecewise_construct,
		std::make_tuple(std::move(body)),
		std::make_tuple(beast::http::status::ok, req.version()) };
	res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
	res.set(beast::http::field::content_type, mime_type(path));
	res.content_length(body.size());
	res.keep_alive(req.keep_alive());
	return send(std::move(res));
}

//------------------------------------------------------------------------------

// Report a failure
void
fail(boost::system::error_code ec, char const* what)
{
	std::cerr << what << ": " << ec.message() << "\n";
}

// Handles an HTTP server connection
class session : public std::enable_shared_from_this<session>
{
	// This is the C++11 equivalent of a generic lambda.
	// The function object is used to send an HTTP message.
	struct send_lambda
	{
		session& self_;

		explicit
			send_lambda(session& self)
			: self_(self)
		{
		}

		template<bool isRequest, class Body, class Fields>
		void
			operator()(beast::http::message<isRequest, Body, Fields>&& msg) const
		{
			// The lifetime of the message has to extend
			// for the duration of the async operation so
			// we use a shared_ptr to manage it.
			auto sp = std::make_shared<
				beast::http::message<isRequest, Body, Fields>>(std::move(msg));

			// Store a type-erased version of the shared
			// pointer in the class to keep it alive.
			self_.res_ = sp;

			// Write the response
			beast::http::async_write(
				self_.socket_,
				*sp,
				asio::bind_executor(
					self_.strand_,
					std::bind(
						&session::on_write,
						self_.shared_from_this(),
						std::placeholders::_1,
						std::placeholders::_2,
						sp->need_eof())));
		}
	};

	tcp::socket socket_;
	asio::strand<asio::io_context::executor_type> strand_;
	beast::flat_buffer buffer_;
	std::string const& doc_root_;
	beast::http::request<beast::http::string_body> req_;
	std::shared_ptr<void> res_;
	send_lambda lambda_;

public:
	// Take ownership of the socket
	explicit
		session(
			tcp::socket socket,
			std::string const& doc_root)
		: socket_(std::move(socket))
		, strand_(socket_.get_executor())
		, doc_root_(doc_root)
		, lambda_(*this)
	{
	}

	// Start the asynchronous operation
	void run()
	{
		do_read();
	}

	void do_read()
	{
		// Read a request
		beast::http::async_read(socket_, buffer_, req_,
			asio::bind_executor(
				strand_,
				std::bind(
					&session::on_read,
					shared_from_this(),
					std::placeholders::_1,
					std::placeholders::_2)));
	}

	void on_read(boost::system::error_code ec, std::size_t bytes_transferred)
	{
		boost::ignore_unused(bytes_transferred);

		// This means they closed the connection
		if (ec == beast::http::error::end_of_stream)
			return do_close();

		if (ec)
			return fail(ec, "read");

		// Send the response
		handle_request(doc_root_, std::move(req_), lambda_);
	}

	void on_write(boost::system::error_code ec, std::size_t bytes_transferred, bool close)
	{
		boost::ignore_unused(bytes_transferred);

		if (ec)
			return fail(ec, "write");

		if (close)
		{
			// This means we should close the connection, usually because
			// the response indicated the "Connection: close" semantic.
			return do_close();
		}

		// We're done with the response so delete it
		res_ = nullptr;

		// Read another request
		do_read();
	}

	void do_close()
	{
		// Send a TCP shutdown
		std::error_code ec;
		socket_.shutdown(tcp::socket::shutdown_send, ec);

		// At this point the connection is closed gracefully
	}
};


// Accepts incoming connections and launches the sessions
struct listener : hla::server
{
	std::string const& doc_root_;

	listener(asio::io_context& context, uint16_t port, std::string const& doc_root) :
		server(context, port),
		doc_root_(doc_root)
	{
	}

	// Start accepting incoming connections
	void run()
	{
		accept([this](tcp::socket& socket)
		{
			// Create the session and run it
			std::make_shared<session>(
				std::move(socket),
				doc_root_)->run();
		});
	}
};