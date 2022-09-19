#include <string>
#include <iostream>
#include <fstream>
#include <thread>
#include "src/CppLogger.hpp"

void loop(int index) {
    CppLogger::CppLogger & log(CppLogger::CppLogger::get_logger());
    log.set_thr_value("THREAD", "loop" + std::to_string(index));

    for (int i=0; i < 1000000; i++) {
        log.info_msg("Hello from " + std::to_string(i));
    }
}


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

    std::thread thr(loop, 0);
    loop(1);
    thr.join();   
}
