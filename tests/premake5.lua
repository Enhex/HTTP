location_dir = "../../HTTP-tests"

include(location_dir .. "/conanpremake.lua")

workspace("HTTP tests")
	configurations { "Debug", "Release" }
	location(location_dir)
	architecture "x86_64"

-- uses [name].cpp as source
function add_test(name)
	project(name)
		kind "ConsoleApp"
		language "C++"
		cppdialect "C++17"
		targetdir = location_dir .. "bin/%{cfg.buildcfg}"

		files{name .. ".cpp"}

		includedirs{conan_includedirs, "../include"}
		libdirs{conan_libdirs}
		links{conan_libs}
		defines{conan_cppdefines, "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"}
		bindirs{conan_bindirs}

		filter "configurations:Debug"
			defines { "DEBUG" }
			symbols "On"

		filter "configurations:Release"
			defines { "NDEBUG" }
			optimize "On"
end

add_test("file_server")
project("file_server")
	debugargs("4000 E:/asio/HTTP/tests 1")