gsc_tool = {
	source = path.join(dependencies.basePath, "gsc-tool"),
}

function gsc_tool.import()
	links { "xsk-gsc-h2", "xsk-gsc-utils" }
	gsc_tool.includes()
end

function gsc_tool.includes()
	includedirs {
		path.join(gsc_tool.source, "include"),
	}
end

function gsc_tool.project()
	project "xsk-gsc-utils"
		kind "StaticLib"
		language "C++"

		files {
			path.join(gsc_tool.source, "include/xsk/utils/*.hpp"),
			path.join(gsc_tool.source, "src/utils/*.cpp"),
		}

		includedirs {
			path.join(gsc_tool.source, "include"),
		}

		zlib.includes()

	project "xsk-gsc-h2"
		kind "StaticLib"
		language "C++"

		filter "action:vs*"
			buildoptions "/Zc:__cplusplus"
		filter {}

		-- These upstream catalogs initialize u8/u16 keys through std::pair's
		-- forwarding constructor. Their literal keys fit the destination types;
		-- MSVC v142 still reports C4244 inside <utility>. Limit the Release
		-- exception to these three tables, retaining /WX for other diagnostics.
		for _, source in ipairs({"h2_code.cpp", "h2_func.cpp", "h2_meth.cpp"}) do
			filter {"action:vs*", "configurations:Release", "files:**/" .. source}
				disablewarnings {"4244"}
		end
		filter {}

		files {
			path.join(gsc_tool.source, "include/xsk/stdinc.hpp"),

			path.join(gsc_tool.source, "include/xsk/gsc/engine/h2.hpp"),
			path.join(gsc_tool.source, "src/gsc/engine/h2.cpp"),

			path.join(gsc_tool.source, "src/gsc/engine/h2_code.cpp"),
			path.join(gsc_tool.source, "src/gsc/engine/h2_func.cpp"),
			path.join(gsc_tool.source, "src/gsc/engine/h2_meth.cpp"),
			path.join(gsc_tool.source, "src/gsc/engine/h2_token.cpp"),

			path.join(gsc_tool.source, "src/gsc/*.cpp"),

			path.join(gsc_tool.source, "src/gsc/common/*.cpp"),
			path.join(gsc_tool.source, "include/xsk/gsc/common/*.hpp"),
		}

		includedirs {
			path.join(gsc_tool.source, "include"),
		}
end

table.insert(dependencies, gsc_tool)
