#include <string>
#include <iostream>


namespace LogLevel {
    enum Levels { DEBUG, INFO, WARNING, ERROR, CRITICAL };
    std::string str(Levels a) {
        switch (a) {
        case DEBUG:
            return std::string("DEBUG");
        case INFO:
            return std::string("INFO");
        case WARNING:
            return std::string("WARNING");
        case ERROR:
            return std::string("ERROR");
        case CRITICAL:
            return std::string("CRITICAL");
        default:
            return std::string("IT IS NON_FULL SWITCH!!!");
        }
    }
};

int main(){
    LogLevel::Levels a = LogLevel::INFO;
    std::cout << LogLevel::str(a) << std::endl;
}
