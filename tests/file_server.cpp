#include <http/file_server.h>
#include <hla/utility.h>

int main(int argc, char* argv[])
{
	// Check command line arguments.
	if (argc != 4)
	{
		std::cerr <<
			"Usage: http-server-async <port> <doc_root> <threads>\n" <<
			"Example:\n" <<
			"    http-server-async 0.0.0.0 8080 . 1\n";
		return EXIT_FAILURE;
	}

	auto const port = static_cast<uint16_t>(std::atoi(argv[1]));
	std::string const doc_root = argv[2];
	auto const threads_count = std::max<int>(1, std::atoi(argv[3]));

	// The io_context is required for all I/O
	boost::asio::io_context context;// { threads_count };

	// Create and launch a listening port
	http::file_server server(context, port, doc_root);
	server.run();

	// Run the I/O service on the requested number of threads
	auto threads = hla::thread_pool_run(context, threads_count);
	hla::join_threads(threads);

	return EXIT_SUCCESS;
}