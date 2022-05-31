#include <string>
#include <map>
#include <thread>
#include "boundedbuffer.hpp"

namespace LogLevel {
    enum Level { DEBUG, INFO, WARNING, ERROR, CRITICAL };
    std::string to_str(Level a) {
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

std::string get_iso_time() {
    auto timepoint = std::chrono::system_clock::now();
    auto coarse = std::chrono::system_clock::to_time_t(timepoint);
    auto fine = std::chrono::time_point_cast<std::chrono::milliseconds>(timepoint);

    char buffer[sizeof "9999-12-31T23:59:59.999Z"];
    std::snprintf(buffer + std::strftime(buffer, sizeof buffer - 4,
                                         "%FT%T.", std::localtime(&coarse)),
                  5, "%03luZ", fine.time_since_epoch().count() % 1000);
    return buffer;
};

class StructMsg {
    private:
        std::string msg;
        std::string rqId;
        LogLevel::Level log_level;
    public:
        StructMsg(std::string st=std::string(), LogLevel::Level l=LogLevel::INFO): msg(st), log_level(l) {}
        void set_msg(std::string && rhs){ msg = rhs; }
        void set_level(LogLevel::Level l){ log_level = l; }
        void set_rqId(const std::string & in){ rqId = in; }
        std::string serialize() { 
            return "{\"level\": \"" + LogLevel::to_str(log_level) + "\", \"time\": \"" + get_iso_time() + "\", \"rqId\": \"" + rqId + "\", \"message\": \"" + msg + "\"}"; 
        }
        ~StructMsg(){}
};

class BaseOutputter {
    protected:
        bounded_buffer< StructMsg & > & buffer;
        virtual void write_msg(std::string) = 0;
        std::atomic<bool> & is_worked;
    public:
        BaseOutputter(bounded_buffer< StructMsg & > & in_buf, std::atomic<bool> & in_a): buffer(in_buf), is_worked(in_a) {}
        void work() {
            while(is_worked.load()) {
                StructMsg * msg;
                buffer.pop_back(msg);
                write_msg(msg->serialize());
            }
        }
};

class ConsoleOutputter: public BaseOutputter {
    protected:
        void write_msg(std::string msg) override { std::cout << msg + "\n"; }
};

class PrimitiveMsg {
    private:
        std::string msg;
        LogLevel::Level log_level;
        std::thread::id thr_id;
        std::atomic<bool> is_ready;
    public:
        PrimitiveMsg(std::string && rhs, LogLevel::Level l, std::thread::id t): msg(rhs), log_level(l), thr_id(t), is_ready(false) {}
};

class CppLogger {
    private:
        std::map<std::thread::id, StructMsg> log_templs;
        bounded_buffer< StructMsg & > buffer;
        BaseOutputter & out;
    public:
        CppLogger(std::string format) {};
        void info_msg(std::string && rhs) { if }
        void put_msg(StructMsg)
};