casclib = { source = path.join(dependencies.basePath, "casclib") }

function casclib.includes()
	includedirs { path.join(casclib.source, "src") }
	defines { "CASCLIB_NO_AUTO_LINK_LIBRARY", "CASCLIB_UNICODE" }
end

function casclib.import()
	-- Only the launcher uses this reader; do not merge its import libraries
	-- into common.lib or impose its headers on unrelated utility sources.
	filter "kind:ConsoleApp"
		casclib.includes()
		links { "casclib", "wininet" }
	filter {}
end

function casclib.project()
	project "casclib"
		kind "StaticLib"
		language "C++"
		characterset "Unicode"
		casclib.includes()
		zlib.includes()
		defines { "CASC_USE_SYSTEM_ZLIB", "CASCLIB_NODEBUG", "_CRT_SECURE_NO_WARNINGS" }
		files {
			path.join(casclib.source, "src/*.cpp"),
			path.join(casclib.source, "src/common/*.cpp"),
			path.join(casclib.source, "src/hashes/*.cpp"),
			path.join(casclib.source, "src/jenkins/lookup3.c"),
			path.join(casclib.source, "src/overwatch/aes.cpp"),
			path.join(casclib.source, "src/overwatch/apm.cpp"),
			path.join(casclib.source, "src/overwatch/cmf.cpp"),
		}
		warnings "Off"
end

table.insert(dependencies, casclib)
