#pragma once

#include <string>

struct URL
{
	URL(std::string&& str) :
		str(std::move(str))
	{
		parse();
	}

	// full URL string
	std::string str;

	// URL parts
	std::string_view scheme;
	std::string_view host;
	std::string_view port;
	std::string_view path;

	void parse();
};

void URL::parse()
{
	auto start = str.begin();
	auto current = start;
	const auto finish = str.end();

	// end a part of the URL structure, returning its content
	auto end_part = [&](uint_fast8_t skip_chars = 1) {
		auto part = std::string_view(&*start, std::distance(start, current - 1)); // `-1` to not include the delimiter
		start = current += skip_chars; // skip delimiter and set a new start
		return part;
	};

	// parse scheme
	for (; current != finish; ++current)
	{
		if (*current == ':') {
			scheme = end_part(3); // skip `://`
			break;
		}
	}

	// ignoring user & password


	auto save_host = [&] {
		host = end_part();
	};

	// parse host
	for (; current != finish; ++current)\
	{
		if (*current == ':')
		{
			save_host();

			auto save_port = [&] {
				port = end_part();
			};

			// parse port (optional)
			for (; current != finish; ++current)
			{
				if (*current == '/') {
					save_port();
					break;
				}
			}

			// nothing after the port
			if (current == finish)
				save_port();

			break;
		}
		else if (*current == '/') {
			save_host();
			break;
		}
	}

	// nothing after the host
	if (current == finish)
		save_host();
	// the rest is the path
	else
		path = std::string_view(&*start, std::distance(start, finish));
}
