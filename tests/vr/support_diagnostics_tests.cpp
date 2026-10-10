#include <std_include.hpp>
#include "component/console_history.hpp"
#include "component/vr/runtime_failure_history.hpp"
#include "component/vr/diagnostics/support_archive.hpp"
#include "component/vr/diagnostics/screen_display.hpp"
#include "component/vr/diagnostics/post_aa.hpp"
#include <utils/compression.hpp>
#include <unzip.h>
#include <iowin32.h>
#include <fstream>

int main()
{
	int failures{};
	const auto check=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
	console::detail::message_history history;
	history.push(1,"initial runtime failure",1);
	for(unsigned i{};i<1000;++i)history.push(7,std::string(1024,'x')+std::to_string(i),i+2);
	history.push(1,"latest runtime failure",2000);
	const auto log=history.format();
	check(log.size()<300*1024&&log.find("initial runtime failure")!=std::string::npos&&
		log.find("latest runtime failure")!=std::string::npos,"bounded history retains first and last errors after ordinary messages roll over");
	history.push(7,"repeated",2001);history.push(7,"repeated",2002);
	check(history.format().find("repeats=2")!=std::string::npos,"duplicate console lines retain repetitions");
	vr::runtime_failure_history events;
	events.record(1,1,3,-2,"native pair","views","failed","runtime","headset");
	for(unsigned i=2;i<=20;++i)events.record(i,i,4,-2,"submit","running","failed","runtime","headset");
	check(events.total==20&&events.retained==8&&events.first.tick==1&&events.first.operation=="native pair",
		"runtime failure history keeps the first cause after later teardown failures");
	events.record(21,20,5,-2,"submit","running","failed","runtime","headset");
	check(events.total==21&&events.retained==8&&events.recent[(events.next+7)%8].repetitions==2,
		"repeated API failure updates the latest occurrence without evicting distinct events");
	const auto temporary=std::filesystem::temp_directory_path();
	{
		namespace aa=vr::diagnostics::post_aa;
		aa::history validation;aa::failure sample;
		sample.tick=10;sample.stage="smaa_scratch_extent";sample.detail="texture_format";
		sample.selected=vr::native_post_aa::mode::smaa_t2x;sample.view={42,3,1,false};
		sample.route={5,4};sample.targets_known=true;sample.failed.id=14;
		sample.failed.texture_known=true;sample.failed.texture.Format=DXGI_FORMAT_R8G8_UNORM;
		sample.reference.id=4;sample.reference.texture_known=true;
		sample.reference.texture.Width=2064;sample.reference.texture.Height=2208;
		validation.record(sample);
		for(unsigned i=0;i<100;++i)
		{
			sample.tick=20+i;sample.detail="texture_extent";sample.failed.id=15;
			sample.selected=vr::native_post_aa::mode::filmic_smaa;sample.view={50+i,4,0,false};
			validation.record(sample);
		}
		const auto report=validation.format(200);
		check(report.size()<8192&&report.find("failures=101")!=std::string::npos&&
			report.find("first_failure: sequence=1")!=std::string::npos&&
			report.find("latest_failure: sequence=101")!=std::string::npos&&
			report.find("detail=texture_format mode=smaa_t2x(3) pair=42 eye=1 generation=3")!=std::string::npos&&
			report.find("detail=texture_extent mode=filmic_smaa(4)")!=std::string::npos&&
			report.find("target=14")!=std::string::npos&&report.find("target=15")!=std::string::npos,
			"AA diagnostics retain bounded first/latest failure context across modes and generations");
	}
	{
		namespace screen=vr::diagnostics::screen;
		screen::trace display;screen::sample sample;
		sample.active=true;sample.tick=10;sample.stage="planned";sample.success=true;sample.hud_rejected=screen::hud_missing;
		display.record(sample);
		check(display.format("Javelin",20).find("rejected_events=0")!=std::string::npos,
			"optional missing Javelin HUD is not a failed scene plan");
		sample.tick=21;sample.stage="compose";sample.success=false;sample.rejected=screen::auxiliary_generation|screen::hud_reference;
		display.record(sample);sample={};sample.tick=22;display.record(sample);
		const auto report=display.format("scope",30);
		check(report.find("first_failure")!=std::string::npos&&report.find("auxiliary_generation")!=std::string::npos&&
			report.find("last_success")!=std::string::npos,"leaving a display preserves its failure and last successful stage");
		for(unsigned i{};i<20;++i)screen::note_material(sample,"hud_javelin_"+std::to_string(i));
		check(sample.material_count==8&&sample.material_overflow==12,"native material candidates remain bounded with explicit overflow");
	}
	const auto root=temporary/std::format("overlord-support-test-{}-{}",GetCurrentProcessId(),GetTickCount64());
	try
	{
		const auto unicode=root/std::filesystem::path(L"\u8bca\u65ad");
		std::filesystem::create_directories(unicode);
		const auto oversized=root/"large.dmp";
		{std::ofstream file(oversized,std::ios::binary);file.seekp(vr::diagnostics::support::archive::file_limit);file.put('x');}
		vr::diagnostics::support::archive archive(unicode,"capture");
		archive.add("status-1.txt","fresh first snapshot\n");
		archive.add("status-2.txt","fresh second snapshot\n");
		archive.include(root/"missing.txt","missing.txt");
		archive.include(oversized,"large.dmp");
		bool rejected{};try{archive.add("../escape.txt","bad");}catch(const std::exception&){rejected=true;}
		check(rejected&&!std::filesystem::exists(unicode/"escape.txt"),"entry names cannot escape the capture");
		const auto zip_path=archive.finish();
		check(std::filesystem::is_regular_file(zip_path)&&!std::filesystem::exists(archive.staging()),"ZIP publication removes only completed staging files");
		zlib_filefunc64_def io{};fill_win32_filefunc64W(&io);
		auto zip=unzOpen2_64(zip_path.c_str(),&io);
		check(zip!=nullptr,"ZIP opens from a Unicode path with a valid central directory");
		if(zip)
		{
			const auto read=[&](const char* name)
			{
				std::string result;
				if(unzLocateFile(zip,name,1)!=UNZ_OK||unzOpenCurrentFile(zip)!=UNZ_OK)return result;
				std::array<char,4096> bytes{};int count{};
				while((count=unzReadCurrentFile(zip,bytes.data(),static_cast<unsigned>(bytes.size())))>0)result.append(bytes.data(),count);
				check(unzCloseCurrentFile(zip)==UNZ_OK,"ZIP entry CRC and close succeed");return result;
			};
			check(read("status-2.txt")=="fresh second snapshot\n","fresh status content survives packaging");
			const auto manifest=read("manifest.json");
			check(manifest.find("missing_or_nonregular")!=std::string::npos&&manifest.find("unreadable_or_size_limit")!=std::string::npos,
				"omitted and unavailable evidence is explicit in the manifest");
			unzClose(zip);
		}
		rejected=false;try{vr::diagnostics::support::archive duplicate(unicode,"capture");}catch(const std::exception&){rejected=true;}
		check(rejected,"duplicate requests never overwrite an existing diagnostic package");
		if(!failures&&root.parent_path()==temporary&&root.filename().string().starts_with("overlord-support-test-"))std::filesystem::remove_all(root);
	}
	catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
	std::cout<<"vr-support-diagnostics-tests: "<<(failures?"FAIL":"PASS")<<'\n';
	return failures?1:0;
}
