openvr = {
	source = path.join(dependencies.basePath, "openvr"),
}

function openvr.import()
	links {"openvr_api"}
	openvr.includes()
end

function openvr.includes()
	includedirs {
		path.join(openvr.source, "headers"),
	}
	defines {"OPENVR_BUILD_STATIC"}
end

function openvr.project()
	project "openvr_api"
		kind "StaticLib"
		language "C++"
		warnings "Off"

		files {
			path.join(openvr.source, "headers/openvr.h"),
			path.join(openvr.source, "src/ivrclientcore.h"),
			path.join(openvr.source, "src/json/**.h"),
			path.join(openvr.source, "src/openvr_api_public.cpp"),
			path.join(openvr.source, "src/jsoncpp.cpp"),
			path.join(openvr.source, "src/vrcore/**.h"),
			path.join(openvr.source, "src/vrcore/**.cpp"),
		}

		includedirs {
			path.join(openvr.source, "headers"),
			path.join(openvr.source, "src"),
			path.join(openvr.source, "src/vrcore"),
		}

		defines {
			"OPENVR_BUILD_STATIC",
			"Json=OpenVRJson",
			"VR_API_PUBLIC",
			"VRCORE_NO_PLATFORM",
			"WIN32",
		}
end

table.insert(dependencies, openvr)
