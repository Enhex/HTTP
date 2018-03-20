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

namespace http
{
	// Report a failure
	inline void fail(boost::system::error_code ec, char const* what)
	{
		std::cerr << what << ": " << ec.message() << "\n";
	}

	/* Handles an HTTP server connection.
	RequestHandler is responsible for writing the response, if any, and starting reading the next request.
	TODO - provide a function that takes a response and writes it back, then starts reading the next request if needed
	*/
	template<typename RequestHandler>
	struct session : std::enable_shared_from_this<session<RequestHandler>>
	{
		tcp::socket socket;
		asio::strand<asio::io_context::executor_type> strand;
		beast::flat_buffer buffer;
		beast::http::request<beast::http::string_body> request;
		RequestHandler request_handler;


		// Take ownership of the socket
		explicit session(tcp::socket&& socket, RequestHandler request_handler)
			: socket(std::move(socket))
			, strand(socket.get_executor())
			, request_handler(std::move(request_handler))
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
			beast::http::async_read(socket, buffer, request,
				asio::bind_executor(
					strand,
					[self = shared_from_this()](boost::system::error_code ec, std::size_t bytes_transferred) {
						self->on_read(ec, bytes_transferred);
					}
				)
			);
		}

		void on_read(boost::system::error_code ec, std::size_t /*bytes_transferred*/)
		{
			// This means they closed the connection
			if (ec == beast::http::error::end_of_stream)
				return do_close();

			if (ec)
				return fail(ec, "read");

			// handle the request
			request_handler(shared_from_this(), std::move(request));
		}

		// call after writing a response
		void on_write(boost::system::error_code ec, std::size_t /*bytes_transferred*/, bool close)
		{
			if (ec)
				return fail(ec, "write");

			if (close)
			{
				// This means we should close the connection, usually because
				// the response indicated the "Connection: close" semantic.
				return do_close();
			}

			// Read another request
			do_read();
		}

		void do_close()
		{
			// Send a TCP shutdown
			boost::system::error_code ec;
			socket.shutdown(tcp::socket::shutdown_send, ec);

			// At this point the connection is closed gracefully
		}
	};


	// Accepts incoming connections and launches the sessions
	struct server : hla::server
	{
		using hla::server::server;

		// accept one new connection at a time asynchronously
		//NOTE: move the socket in OnAccept to control its life-time
		template<typename RequestHandler>
		void accept(const RequestHandler&& request_handler)
		{
			hla::server::accept([request_handler{std::move(request_handler)}](tcp::socket& socket)
			{
				// Create the session and run it
				std::make_shared<session<decltype(request_handler)>>(std::move(socket), std::move(request_handler))->run();
			});
		}
	};
}