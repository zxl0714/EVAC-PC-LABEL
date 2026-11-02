#include "backends/ps2/input_trace.h"
#include <thread>
#include <sstream>
#include <string>
#include <iostream>

namespace {
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
}
int main() {
    namespace trace=cave::ps2::input_trace;
    const auto directory=std::filesystem::temp_directory_path()/
        ("evac-input-trace-"+std::to_string(trace::Stamp()));
    try {
        Require(!trace::Enabled(),"Normal input unexpectedly enabled tracing");
        trace::Record(trace::Kind::HostKey);trace::Tick(1);
        std::filesystem::create_directory(directory);
        const auto path=directory/"authored.csv";
        {
            trace::Scope scope(path,401);
            trace::Tick(39);
            auto record=[](trace::Kind kind){for(unsigned i=0;i<200;++i)trace::Record(kind,0,i);};
            std::thread a(record,trace::Kind::HostKey),b(record,trace::Kind::PadRead);a.join();b.join();
            Require(!std::filesystem::exists(path),"Trace wrote to disk during frame execution");
            trace::Save();
        }
        Require(!trace::Enabled(),"Input trace outlived the joined session");
        std::ifstream input(path);std::string line;unsigned keys=0,reads=0,rows=0;
        std::getline(input,line);Require(line=="# format=1 recorded=401 overflow=0","Trace lost a writer's row");
        std::getline(input,line);Require(line=="sequence,qpc_us,kind,frame,a,b,c","Input trace schema changed");
        while(std::getline(input,line)) {
            ++rows;keys+=line.find(",host_key,39,")!=std::string::npos;
            reads+=line.find(",pad_read,39,")!=std::string::npos;
        }
        Require(rows==401 && keys==200 && reads==200,"Trace mixed field identities or simultaneous writers");
        const auto capped=directory/"capped.csv";
        {trace::Scope scope(capped,1);trace::Tick(1);trace::Tick(2);trace::Save();}
        std::ifstream cap(capped);std::getline(cap,line);
        Require(line=="# format=1 recorded=1 overflow=1","Trace silently overwrote its bounded storage");
        bool rejected=false;
        try{trace::Scope scope(path);}catch(const std::invalid_argument&){rejected=true;}
        Require(rejected,"Input trace replaced an existing diagnostic file");
        std::cout<<"PS2 input trace: disabled default, two writers, field IDs, deferred IO, bounds and preservation: PASS\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; evidence="<<directory<<'\n';return 1;}
}
