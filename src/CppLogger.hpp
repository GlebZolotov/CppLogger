#include <string>
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

template<class T>
class StructMsg {
    private:
        LogLevel::Level log_level;
        T struct_msg;
    public:
        StructMsg(const T & in_msg, LogLevel::Level ll): struct_msg(in_msg), log_level(ll) {};
        StructMsg & operator=(StructMsg&& rhs) { std::swap(struct_msg, rhs.struct_msg); log_level = rhs.log_level; return *this; };
        std::string serialize() { struct_msg.set_level(log_level); return struct_msg.serialize(); };
        ~StructMsg() {};
};

template<class T>
class BaseOutput {
    protected:
        bounded_buffer< StructMsg<T> > & buffer;
        virtual void send_msg(std::string) = 0;
        std::atomic
    public:
        BaseOutput(bounded_buffer< StructMsg<T> > & in_buf): buffer(in_buf) {};
        void work() {
            while()
        }


}

class CppLogger {
    public:
        CppLogger() {};
};