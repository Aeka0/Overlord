#pragma once

#include "component/vr/diagnostics/input_manifest.hpp"
#include <chrono>
#include <sstream>

template<class Check>
void input_manifest_tests(const Check& check)
{
	using namespace vr::diagnostics;
	const auto folder=std::filesystem::temp_directory_path()/
		("overlord-input-diagnostics-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directory(folder);
	const auto path=folder/u8"\u624b\u67c4.json";
	const auto write=[&](const std::string& bytes) {std::ofstream file(path,std::ios::binary);file<<bytes;};
	std::ostringstream report;
	write(R"({"actions":[],"default_bindings":[]})");
	check(read_input_manifest(report,path).is_object(),"input diagnostics read UTF-8 paths");
	write("{broken");report.str("");report.clear();
	check(read_input_manifest(report,path).is_null() && report.str().find("state=invalid_json")!=std::string::npos,
		"malformed installed input files are reportable without throwing");
	write(std::string(1024*1024+1,' '));report.str("");report.clear();
	check(read_input_manifest(report,path).is_null() && report.str().find("over_1MiB")!=std::string::npos,
		"oversized manifests are bounded before allocation and parsing");
	write("{\"nested\":"+std::string(40,'[')+"0"+std::string(40,']')+"}");
	report.str("");report.clear();
	check(read_input_manifest(report,path).is_null() && report.str().find("nesting exceeds")!=std::string::npos,
		"excessive JSON nesting is bounded");
	std::filesystem::remove(path);
	report.str("");report.clear();
	check(read_input_manifest(report,path).is_null() && report.str().find("state=unavailable")!=std::string::npos,
		"missing input resources have an explicit diagnostic");
	std::filesystem::remove(folder);
	check(!local_input_binding("../outside.json") && !local_input_binding("https://example.org/binding.json") &&
		!local_input_binding(std::filesystem::temp_directory_path()/"absolute.json") && local_input_binding("oculus_touch.json"),
		"binding inspection stays local to the input package");
	check(input_quoted("first\nsecond").find('\n')==std::string::npos &&
		input_quoted(std::string(2000,'a')).size()<550,
		"diagnostic strings cannot inject lines or grow without a bound");
}
