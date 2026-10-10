return function()
	require("vstudio")
	local p = premake
	local vc = p.vstudio.vc2010
	-- beta2 exports dependson only to the solution. Resource-producing projects
	-- must also run before direct .vcxproj builds, without linking their outputs.
	-- https://github.com/premake/premake-core/blob/v5.0.0-beta2/modules/vstudio/vs2010_vcxproj.lua
	p.override(vc, "projectReferences", function(base, prj)
		base(prj)
		local linked = p.project.getdependencies(prj, "linkOnly")
		local dependencies = {}
		for _, ref in ipairs(p.project.getdependencies(prj, "dependOnly")) do
			if not table.contains(linked, ref) then
				table.insert(dependencies, ref)
			end
		end
		if #dependencies == 0 then return end
		p.push('<ItemGroup>')
		for _, ref in ipairs(dependencies) do
			local relative = p.vstudio.path(prj, p.vstudio.projectfile(ref))
			p.push('<ProjectReference Include="%s">', relative)
			vc.referenceProject(prj, ref)
			p.out('<LinkLibraryDependencies>false</LinkLibraryDependencies>')
			p.out('<ReferenceOutputAssembly>false</ReferenceOutputAssembly>')
			p.pop('</ProjectReference>')
		end
		p.pop('</ItemGroup>')
	end)
end
