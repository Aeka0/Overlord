openxr = {
	source = path.join(dependencies.basePath, "openxr"),
}

function openxr.import()
	openxr.includes()
end

function openxr.includes()
	includedirs {
		path.join(openxr.source, "include")
	}
end

function openxr.project()
end

table.insert(dependencies, openxr)
