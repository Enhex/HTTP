from conans import ConanFile, tools
import os


class HttpConan(ConanFile):
	name = "HTTP"
	version = "master"
	description = "HTTP library on top of Boost Beast and High Level Asio."
	no_copy_source = True
	exports_sources = "include/*"
	requires = (
		"high_level_asio/master@enhex/stable",
		"boost_beast/1.66.0@bincrafters/stable"
	)

	def configure(self):
		# always use Boost.Asio since Boost.Beast uses it
		self.options["high_level_asio"].asio_standalone = False
	
	def package(self):
		self.copy("*.h")
