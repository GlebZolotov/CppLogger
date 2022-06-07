#include <string>
#include <iostream>
#include <fstream>
#include "src/CppLogger.hpp"


int main(){
    CppLogger & log(CppLogger::get_logger());

    std::ifstream t("../format.json");
    std::stringstream b;
    b << t.rdbuf();

    log.set_base_format(b.str());
    log.set_glob_value(std::string("VERS"), std::string("v0.0.1"));

    log.set_thr_value("THREAD", "main");

    log.info_msg("Hello");
}
