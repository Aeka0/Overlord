"""Exercise production HTTP, worker and updater request ownership on loopback."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import os
import subprocess
import tempfile
import threading


root = Path(__file__).resolve().parents[1]
source = (root / "src/client/component/updater.cpp").read_text(encoding="utf-8-sig")


def function(name, indent="\t\t", contents=source):
    start = contents.index(indent + name)
    opening = contents.index("\n" + indent + "{", start) + len(indent) + 1
    depth = 1
    end = opening + 1
    while depth:
        depth += (contents[end] == "{") - (contents[end] == "}")
        end += 1
    return contents[start:end]


# Compile the production ownership/state functions unchanged; engine UI and
# installation operations are replaced at their boundaries, never executed.
state = source[source.index("\t\tstruct status"):source.index("\t\tstd::unordered_map")]
functions = "\n".join(function(name, indent) for name, indent in (
    ("void notify(", "\t\t"),
    ("void set_update_check_status(", "\t\t"),
    ("void set_update_download_status(", "\t\t"),
    ("utils::http::request_options options_for(", "\t\t"),
    ("void cancel_update()", "\t"),
    ("void start_update_check()", "\t"),
    ("void start_update_download()", "\t"),
))
# Exercise the production paused lifecycle with IO/command boundaries replaced.
motd_source = (root / "src/client/component/motd.cpp").read_text(encoding="utf-8-sig")
motd_switch = motd_source[motd_source.index("// Temporarily paused"):motd_source.index("namespace motd")]
motd_state = motd_source[motd_source.index("\t\tstruct wordle_data_t"):motd_source.index("\t\tstruct cached_file_header")]
motd_component = motd_source[motd_source.index("\tclass component final"):motd_source.index("\n}\n\nREGISTER_COMPONENT")]
motd_functions = "\n".join(function(name, indent, motd_source) for name, indent in (
    ("void init_links(", "\t\t"), ("bool is_enabled()", "\t"),
    ("links_map_t get_links()", "\t"), ("bool has_motd()", "\t"), ("bool has_wordle()", "\t")))
motd_harness = motd_switch + r'''
namespace paused_motd {
    using links_map_t = std::unordered_map<std::string, std::string>;
    struct component_interface {
        virtual void post_start() {} virtual void post_unpack() {} virtual void pre_destroy() {}
    };
    namespace command { inline int registrations{};
        template<typename F> void add(const char*, F) { ++registrations; }
    }
    std::atomic_int initializations{};
    [[maybe_unused]] void init(bool = true) { ++initializations; }
    std::atomic_bool killed{};
    std::thread init_thread;
''' + motd_state + motd_functions + motd_component + r'''
    void verify() {
        require(!is_enabled(), "legacy MOTD unexpectedly enabled");
        motd_data.access([](auto& data) { data.wordle.valid = true; data.marketing = {{"motd", nlohmann::json::object()}}; });
        component service;
        service.post_start(); service.post_unpack();
        const auto links = get_links();
        require(links.at("github") == product::repository_url && links.contains("credits_1"), "paused service lost project or attribution links");
        require(!has_motd() && !has_wordle(), "paused service retained stale content");
        require(!init_thread.joinable() && initializations == 0 && command::registrations == 0,
            "paused service started IO or registered manual reload commands");
        service.pre_destroy();
    }
}
'''

harness = r'''
#include <utils/http.hpp>
#include <utils/latest_task_worker.hpp>
#include <utils/concurrency.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <future>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_set>
#include <json.hpp>
#include <product.hpp>
using namespace std::chrono_literals;
#define ERR_UPDATE_CHECK_FAIL "check failed"
#define ERR_DOWNLOAD_FAIL "download failed: "
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
namespace scheduler {
    enum class pipeline { lui };
    std::mutex mutex;
    std::deque<std::function<void()>> notifications;
    void once(std::function<void()> fn, pipeline) {
        const std::lock_guard lock(mutex); notifications.push_back(std::move(fn));
    }
    void drain() {
        std::deque<std::function<void()>> batch;
        { const std::lock_guard lock(mutex); batch.swap(notifications); }
        for (auto& fn : batch) fn();
    }
}
namespace ui_scripting {
    int notifications{};
    void notify(const std::string&, std::initializer_list<int>) { ++notifications; }
}
namespace database { void close_fastfile_handles() {} }
namespace console { template<typename... T> void info(const char*, T...) {} }
namespace updater {
''' + state + r'''
    bool backend_available = true;
    bool updates_available() { return backend_available; }
    std::string origin;
    std::atomic_bool request_entered{}, request_returned{};
    std::atomic_int checks{}, downloads{};
    void perform_update_check(request_id);
    void perform_update_download(request_id, const std::vector<file_info>&, const std::vector<std::string>&);
    void task_failed(request_id, bool, std::exception_ptr);
''' + functions + r'''
    void perform_update_check(request_id id) {
        const auto ordinal = ++checks;
        const auto options = options_for(id, false);
        const auto body = utils::http::get_data(origin + (ordinal == 1 ? "/stall" : "/ok"), {},
            [](size_t, size_t, size_t) { request_entered = true; }, options);
        set_update_check_status(id, true, bool(body));
        request_returned = true;
    }
    void perform_update_download(request_id id, const std::vector<file_info>&, const std::vector<std::string>&) {
        ++downloads;
        set_update_download_status(id, true, true);
    }
    void task_failed(request_id id, bool checking, std::exception_ptr) {
        if (checking) set_update_check_status(id, true, false);
        else set_update_download_status(id, true, false);
    }
}
''' + motd_harness + r'''
template<typename P> void await(P predicate, const char* text) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < deadline, text);
        std::this_thread::sleep_for(5ms);
    }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "missing loopback origin");
        paused_motd::verify();
        const std::string origin = argv[1];
        require(utils::http::get_data(origin + "/ok") == "ok!", "normal HTTP response");
        utils::http::request_options limited;
        limited.max_response_bytes = 2;
        require(!utils::http::get_data(origin + "/ok", {}, {}, limited), "response limit");
        limited = {};
        limited.timeout = 150ms;
        const auto before = std::chrono::steady_clock::now();
        require(!utils::http::get_data(origin + "/stall", {}, {}, limited), "stalled request timeout");
        require(std::chrono::steady_clock::now() - before < 2s, "request exceeded deadline");
        limited.timeout = 0ms;
        require(!utils::http::get_data(origin + "/ok", {}, {}, limited), "zero timeout rejected");
        limited = {};
        limited.cancelled = [] { return true; };
        require(!utils::http::get_data(origin + "/ok", {}, {}, limited), "pre-cancelled request");

        utils::latest_task_worker worker;
        std::promise<void> started, release, latest, recovered;
        auto gate = release.get_future().share();
        auto first = started.get_future();
        auto last = latest.get_future();
        auto recovery = recovered.get_future();
        std::atomic_int obsolete{}, failures{};
        auto failed = [&](std::exception_ptr) { ++failures; };
        require(worker.replace([&] { started.set_value(); gate.wait_for(5s); }, failed), "worker admission");
        require(first.wait_for(2s) == std::future_status::ready, "worker did not start");
        for (int i = 0; i < 100; ++i) worker.replace([&] { ++obsolete; }, failed);
        worker.replace([&] { latest.set_value(); }, failed);
        release.set_value();
        require(last.wait_for(2s) == std::future_status::ready && !obsolete, "pending requests not coalesced");
        worker.replace([] { throw std::runtime_error("fixture"); }, failed);
        await([&] { return failures == 1; }, "task failure was not delivered");
        worker.replace([&] { recovered.set_value(); }, failed);
        require(recovery.wait_for(2s) == std::future_status::ready, "worker stopped after exception");
        worker.stop();
        require(!worker.replace([] {}, failed), "admission remained open after stop");
        utils::latest_task_worker self_closing;
        std::promise<void> closed;
        auto self_closed = closed.get_future();
        self_closing.replace([&] { self_closing.stop(); closed.set_value(); }, failed);
        require(self_closed.wait_for(2s) == std::future_status::ready, "callback tried to join itself");
        self_closing.stop();

        updater::backend_available = false;
        updater::start_update_check();
        updater::start_update_download();
        require(updater::checks == 0 && updater::downloads == 0 && updater::request_generation == 0,
            "unconfigured updater must not enqueue requests");
        updater::backend_available = true;
        updater::origin = origin;
        updater::start_update_check();
        await([] { return updater::request_entered.load(); }, "HTTP worker did not enter");
        std::this_thread::sleep_for(100ms);
        const auto old_id = updater::request_generation.load();
        // This uses the same bounded scheduler admission as the real UI bridge.
        // Cancellation must also invalidate notifications queued before it.
        updater::notify(old_id, "obsolete");
        updater::cancel_update();
        updater::start_update_check();
        require(!updater::publish(old_id, [](auto& data) { data.error = "stale"; }), "old result admitted");
        await([] { return updater::update_data.access<bool>([](auto& data) { return data.check.done; }); }, "restart blocked by cancelled HTTP");
        require(updater::update_data.access<bool>([](auto& data) { return data.check.success && data.error.empty(); }), "old request corrupted new state");
        scheduler::drain();
        require(ui_scripting::notifications == 1, "stale UI notification delivered");
        updater::start_update_download();
        await([] { return updater::downloads == 1; }, "download did not start");
        await([] { return updater::update_data.access<bool>([](auto& data) { return data.download.done; }); }, "download did not complete");
        updater::start_update_download();
        require(updater::downloads == 1, "completed download admitted twice");
        updater::cancel_update();
        updater::update_worker.stop();
        scheduler::drain();
        require(ui_scripting::notifications == 1, "notification survived shutdown");

        // A stalled owner can be cancelled and joined without waiting for its
        // long payload timeout; no worker thread is detached at destruction.
        utils::latest_task_worker closing;
        std::atomic_bool entered{}, returned{};
        closing.replace([&] {
            utils::http::request_options options;
            options.cancelled = [&] { return closing.stopping(); };
            (void)utils::http::get_data(origin + "/stall", {},
                [&](size_t, size_t, size_t) { entered = true; }, options);
            returned = true;
        }, [&](std::exception_ptr) { returned = true; });
        await([&] { return entered.load(); }, "closing HTTP did not enter");
        std::this_thread::sleep_for(100ms);
        const auto shutdown = std::chrono::steady_clock::now();
        closing.stop();
        require(returned && std::chrono::steady_clock::now() - shutdown < 3s, "shutdown waited for HTTP timeout");
        std::puts("PASS: paused MOTD/static links; unconfigured updater; HTTP timeout/cancel/limits; worker/recovery; stale UI/shutdown");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        updater::cancel_update(); updater::update_worker.stop();
        return 1;
    }
}
'''


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/stall":
            self.server.stopping.wait(5)
            return
        self.send_response(200)
        self.send_header("Content-Length", "3")
        self.end_headers()
        self.wfile.write(b"ok!")

    def log_message(self, *_):
        pass


vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
installations = json.loads(subprocess.check_output([
    str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json", "-utf8"
], encoding="utf-8"))
if not installations:
    raise RuntimeError("MSVC C++ build tools are required")
vcvars = Path(installations[0]["installationPath"]) / "VC/Auxiliary/Build/vcvars64.bat"
curl = root / "build/bin/x64/RelWithDebInfo/curl.lib"
if not curl.is_file():
    raise RuntimeError("Build the RelWithDebInfo curl target first")

with tempfile.TemporaryDirectory(prefix="h2-background-update-") as taskdir:
    out = Path(taskdir)
    (out / "background_update_tests.cpp").write_text(harness, encoding="utf-8")
    driver = out / "verify.cmd"
    driver.write_text(
        f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
        f'cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /utf-8 /DCURL_STATICLIB '
        f'/I"{root / "src/common"}" /I"{root / "deps/GSL/include"}" '
        f'/I"{root / "deps/curl/include"}" /I"{root / "src/client"}" '
        f'/I"{root / "deps/json/single_include/nlohmann"}" background_update_tests.cpp '
        f'"{root / "src/common/utils/http.cpp"}" /Fe:background_update_tests.exe '
        f'/link "{curl}" ws2_32.lib crypt32.lib advapi32.lib secur32.lib normaliz.lib\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(driver)], cwd=out, check=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    server.stopping = threading.Event()
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    try:
        subprocess.run([str(out / "background_update_tests.exe"),
                        f"http://127.0.0.1:{server.server_port}"], cwd=out, check=True, timeout=20)
    finally:
        server.stopping.set()
        server.shutdown()
        server.server_close()
        serving.join()
