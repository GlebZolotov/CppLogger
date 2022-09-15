#include <string>
#include <iostream>
#include <fstream>
#include "src/CppLogger.hpp"


int main(){
    CppLogger::Options settings;
    settings.host = "localhost";
    settings.port = "24224";
    settings.path_to_file = "test.log";
    settings.path_to_template = "../format.json";
    settings.level = CppLogger::LogLevel::INFO;
    settings.buf_size = 1000;
    settings.output_type = "http";

    CppLogger::CppLogger & log(CppLogger::CppLogger::get_logger(&settings));

    std::ifstream t("../format.json");
    std::stringstream b;
    b << t.rdbuf();

    log.set_base_format(b.str());
    log.set_glob_value(std::string("VERS"), std::string("v0.0.1"));

    log.set_thr_value("THREAD", "main");

    log.info_msg("Hello");
    log.info_msg("World");

    std::this_thread::sleep_for(std::chrono::seconds(5));
}
