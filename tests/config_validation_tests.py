"""Exercise production config reads/writes with in-memory I/O and policy providers."""
from pathlib import Path
import json
import os
import subprocess


root = Path(__file__).resolve().parents[1]
source = (root / "src/client/component/config.cpp").read_text(encoding="utf-8")
# Follow the native adapter tests: compile the production namespace unchanged,
# replacing only its game/OS endpoints. No user's config file is read or written.
body = source[source.index("namespace config"):source.index("REGISTER_COMPONENT(config::component)")]
out = root / "build/config-validation-tests"
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "component/config.hpp"
#include "loader/component_interface.hpp"

#define OLD_CONFIG_FILE "test-old-config.json"
namespace fixture {
    std::optional<std::string> contents;
    unsigned writes{};
    void require(bool valid, const char* message) {
        if (!valid) throw std::runtime_error(message);
    }
}
namespace utils::properties {
    std::filesystem::path get_appdata_path() { return "test-config"; }
}
namespace utils::io {
    bool file_exists(const std::string&) { return fixture::contents.has_value(); }
    std::string read_file(const std::string&) { return fixture::contents.value(); }
    bool write_file(const std::string&, const std::string& data, bool = false) {
        fixture::contents = data;
        ++fixture::writes;
        return true;
    }
    bool remove_file(const std::string&) { fixture::contents.reset(); return true; }
}
namespace console { void error(const char*, const char*) {} }
namespace language {
    std::string get_default_language() { return "english"; }
    bool is_valid_language(const std::string& name) {
        return name == "english" || name == "german";
    }
}
namespace updater {
    // Release-channel selection is a separate policy. Test validation against
    // a supported configured default without choosing a channel for VR builds.
    std::string get_git_branch() { return "develop"; }
    bool is_valid_git_branch(const std::string& branch) {
        return branch == "develop" || branch == "main";
    }
}
'''
harness += body
harness += r'''
int main() {
    using nlohmann::json;
    using fixture::require;
    try {
        for (const auto& key : {"disable_custom_fonts", "language", "motd_last_seen",
                               "motd_last_wordle", "motd_wordle_score", "branch"}) {
            const auto value = config::get_default_value(key);
            require(value.has_value(), "known field lost its default");
            fixture::contents.reset();
            require(config::get_raw(key) == *value && config::get<json>(key) == value,
                    "missing-file reads disagree about defaults");
            require(config::validate_config_field(key, *value) == *value,
                    "default is not stable under validation");
            for (const auto& raw : {"{}", "[]", "null", "123"}) {
                fixture::contents = raw;
                require(config::get_raw(key) == *value && config::get<json>(key) == value,
                        "missing field/non-object document lost its default");
            }
        }

        const auto check_invalid = [](const std::string& key, const json& bad) {
            const auto fallback = config::get_default_value(key).value();
            require(config::validate_config_field(key, bad) == fallback,
                    "invalid value passed the production validator");
            fixture::contents = json{{key, bad}, {"untouched", "keep"}}.dump();
            const auto original = fixture::contents;
            const auto writes = fixture::writes;
            require(config::get_raw(key) == fallback && config::get<json>(key) == fallback,
                    "read entry point bypassed validation");
            if (fallback.is_string())
                require(config::get<std::string>(key).value() == fallback.get<std::string>(),
                        "typed string read bypassed validation");
            require(fixture::contents == original && fixture::writes == writes,
                    "validation unexpectedly rewrote config during read");
            config::set(key, bad);
            const auto saved = json::parse(fixture::contents.value());
            require(saved[key] == fallback && saved["untouched"] == "keep" &&
                    fixture::writes == writes + 1, "set persisted invalid input or damaged another field");
        };

        for (const auto& text : {std::string{}, std::string("unsupported"),
                                std::string(600, 'x'), std::string(1024 * 1024, 'x'),
                                std::string("english\0suffix", 14),
                                std::string("\xe4\xb8\xad\xe6\x96\x87")})
            check_invalid("language", text);
        for (const auto& key : {"language", "branch"})
            for (const json bad : {json(nullptr), json(false), json(17), json(2.5),
                                   json::array({"english"}), json::object()})
                check_invalid(key, bad);
        check_invalid("branch", "feature/invalid-channel");
        check_invalid("disable_custom_fonts", "true");
        check_invalid("disable_custom_fonts", 1);

        for (const auto& key : {"motd_last_seen", "motd_last_wordle", "motd_wordle_score"}) {
            require(config::get_default_value(key)->is_number_unsigned(),
                    "unsigned field has a signed default");
            for (const json bad : {json(-1), json(1.5), json("5"), json(nullptr)})
                check_invalid(key, bad);
            require(config::validate_config_field(key, json(42u)) == 42u,
                    "valid unsigned value changed");
        }
        const json valid_values{{"language", "german"}, {"branch", "main"},
                                {"disable_custom_fonts", true}, {"motd_last_seen", 42u}};
        for (const auto& [key, value] : valid_values.items()) {
            fixture::contents = "{}";
            config::set(key, value);
            require(config::get_raw(key) == value, "valid value failed to round trip");
        }
        const json extension{{"caption", "\xe4\xb8\xad\xe6\x96\x87"}, {"items", {1, 2, 3}}};
        fixture::contents = "{}";
        config::set("mod_extension", extension);
        require(config::get_raw("mod_extension") == extension &&
                config::get<json>("mod_extension").value() == extension,
                "unknown extension field was constrained by the built-in schema");
        require(!config::get_default_value("missing") && !config::get<json>("missing") &&
                config::get_raw("missing").is_null(), "unknown missing key acquired a default");
        std::cout << "config-validation-tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "config-validation-tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
'''
(out / "config_validation_tests.cpp").write_text(harness, encoding="utf-8")
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
installations = json.loads(subprocess.check_output([
    str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json", "-utf8"
], encoding="utf-8"))
if not installations:
    raise RuntimeError("MSVC C++ build tools are required")
vcvars = Path(installations[0]["installationPath"]) / "VC/Auxiliary/Build/vcvars64.bat"
for configuration, options in (("Debug", "/Od /Zi"), ("Release", "/O2 /DNDEBUG")):
    driver = out / (configuration + ".cmd")
    driver.write_text(
        f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
        f'cl /nologo /std:c++20 /EHsc /W4 /WX {options} '
        f'/I"{root / "src/client"}" /I"{root / "deps/json/single_include"}" '
        f'config_validation_tests.cpp /Fo:{configuration}.obj /Fe:{configuration}.exe\n'
        f'if errorlevel 1 exit /b 1\n{configuration}.exe\n', encoding="utf-8")
    print(configuration, flush=True)
    subprocess.run(["cmd.exe", "/d", "/c", str(driver)], cwd=out, check=True)
