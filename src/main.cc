#include <iostream>
#include "./../ext/headers/args.hxx"
#include "cpu.h"

using namespace dramsim3;

int main(int argc, const char **argv) {
    
    args::ArgumentParser parser(
        "DRAM Simulator.",
        "Examples: \n."
        "./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -c 100 -t "
        "sample_trace.txt\n"
        "./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -s random -c 100");
    args::HelpFlag help(parser, "help", "Display the help menu", {'h', "help"});
    //MZOU
    // args::ValueFlag<uint64_t> start_cycles_arg(parser, "start_cycles",
    //                                         "Start cycles to simulate",
    //                                         {'z', "start_cycles"}, 0);
    //MZOU
    args::ValueFlag<uint64_t> num_cycles_arg(parser, "num_cycles",
                                             "Number of cycles to simulate",
                                             {'c', "cycles"}, 100000);
    args::ValueFlag<std::string> output_dir_arg(
        parser, "output_dir", "Output directory for stats files",
        {'o', "output-dir"}, ".");
    args::ValueFlag<std::string> stream_arg(
        parser, "stream_type", "address stream generator - (random), stream",
        {'s', "stream"}, "");
    args::ValueFlag<std::string> trace_file_arg(
        parser, "trace",
        "Trace file, setting this option will ignore -s option",
        {'t', "trace"});
    args::Positional<std::string> config_arg(
        parser, "config", "The config file name (mandatory)");

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help const&) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError const&) {
        //std::cerr << e.what() << std::endl;
        std::cerr << "ParserError" << std::endl;
	    std::cerr << parser;
        return 1;
    }

    //ini文件指定
    std::string config_file = args::get(config_arg);
    if (config_file.empty()) {
        std::cerr << parser;
        return 1;
    }

    //运行多少个cycle
    uint64_t cycles = args::get(num_cycles_arg);
    //开始cycle是多少
    // uint64_t start_cycles = args::get(start_cycles_arg);
    // std::cout << "clk: " << cycles << ", start_cycles: " << start_cycles_arg << std::endl;

    //指定输出文件
    std::string output_dir = args::get(output_dir_arg);
    //如果是trace模式，指定example trace
    std::string trace_file = args::get(trace_file_arg);
    //如果不是trace模式，指定stream还是random
    std::string stream_type = args::get(stream_arg);

    CPU *cpu;
    if (!trace_file.empty()) {
        cpu = new TraceBasedCPU(config_file, output_dir, trace_file);
    } else {
        if (stream_type == "stream" || stream_type == "s") {
            cpu = new StreamCPU(config_file, output_dir);
        } else {
            cpu = new RandomCPU(config_file, output_dir);
        }
    }

    for (uint64_t clk = 0; clk < cycles; clk++) {
        cpu->ClockTick();
    }

    cpu->PrintStats();

    //delete cpu;

    return 0;
}
