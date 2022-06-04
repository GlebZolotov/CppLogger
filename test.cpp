#include <string>
#include <iostream>
#include "src/CppLogger.hpp"


int main(){
    CppLogger & log(CppLogger::get_logger());
    std::cout << std::this_thread::get_id() << std::endl;
}
