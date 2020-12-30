#include "dram_system.h"

#include <assert.h>

namespace dramsim3 {

// alternative way is to assign the id in constructor but this is less
// destructive
int BaseDRAMSystem::total_channels_ = 0;

BaseDRAMSystem::BaseDRAMSystem(Config &config, const std::string &output_dir,
                               std::function<void(uint64_t)> read_callback,
                               std::function<void(uint64_t)> write_callback)
    : read_callback_(read_callback),
      write_callback_(write_callback),
      last_req_clk_(0),
      config_(config),
      timing_(config_),
      //MZOU
      concurrent_serve(0),
      active_cycles(0),
      last_concurrent_serve(0),
      last_active_cycles(0),
      last_read_cmds(0),
      last_write_cmds(0),
      last_read_hits(0),
      last_write_hits(0),
      //MZOU
#ifdef THERMAL
      thermal_calc_(config_),
#endif  // THERMAL
      clk_(0) {
    total_channels_ += config_.channels;
    file = fopen("./dramsim3_output", "w");

#ifdef ADDR_TRACE
    std::string addr_trace_name = config_.output_prefix + "addr.trace";
    address_trace_.open(addr_trace_name);
#endif
}

int BaseDRAMSystem::GetChannel(uint64_t hex_addr) const {
    hex_addr >>= config_.shift_bits;
    return (hex_addr >> config_.ch_pos) & config_.ch_mask;
}

void BaseDRAMSystem::PrintEpochStats() {
    // first epoch, print bracket
    if (clk_ - config_.epoch_period == 0) {
        std::ofstream epoch_out(config_.json_epoch_name, std::ofstream::out);
        epoch_out << "[";
    }
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->PrintEpochStats();
        std::ofstream epoch_out(config_.json_epoch_name, std::ofstream::app);
        epoch_out << "," << std::endl;
    }
#ifdef THERMAL
    thermal_calc_.PrintTransPT(clk_);
#endif  // THERMAL
    return;
}

void BaseDRAMSystem::stats_mo(uint64_t cycle)
{
    uint64_t read_cmds = 0, write_cmds = 0, read_row_hit = 0, write_row_hit = 0;
    for(size_t i = 0; i < ctrls_.size(); i++)
    {
        read_cmds += ctrls_[i]->ReturnReadCmds_Epoch();
        write_cmds += ctrls_[i]->ReturnWriteCmds_Epoch();
        read_row_hit += ctrls_[i]->ReturnReadRowHits_Epoch();
        write_row_hit += ctrls_[i]->ReturnWriteRowHits_Epoch();
    }
    uint64_t current_read_cmds = read_cmds - last_read_cmds;
    uint64_t current_write_cmds = write_cmds - last_write_cmds;
    uint64_t current_read_hit = read_row_hit - last_read_hits;
    uint64_t current_write_hit = write_row_hit - last_write_hits; 
    fprintf(file, "Phase %lu %f %f\n", cycle/10000, (float)(current_read_hit + current_write_hit) / (current_read_cmds + current_write_cmds), (float)(concurrent_serve-last_concurrent_serve) / (active_cycles-last_active_cycles));
    fprintf(file, "Total read commands: %lu, write commands: %lu, read row hit: %lu, write row hit: %lu\n", current_read_cmds, current_write_cmds, current_read_hit, current_write_hit);
    fprintf(file, "DRAM active cycles: %lu, concurrent serve: %lu\n", active_cycles-last_active_cycles, concurrent_serve-last_concurrent_serve);
    fprintf(file, "Read row buffer hit rate: %f, write row buffer hit rate: %f\n", (float)current_read_hit/current_read_cmds, (float)current_write_hit/current_write_cmds);
    // fprintf(file, "Row_Buffer_Hit_Rate %f\n", (float)(current_read_hit + current_write_hit) / (current_read_cmds + current_write_cmds));
    // fprintf(file, "Bank_Level_Parallelism %f\n", (float)(concurrent_serve-last_concurrent_serve) / (active_cycles-last_active_cycles));
    fprintf(file, "\n");
    
    last_read_cmds = read_cmds;
    last_write_cmds = write_cmds;
    last_read_hits = read_row_hit;
    last_write_hits = write_row_hit;
    last_concurrent_serve = concurrent_serve;
    last_active_cycles = active_cycles;
}

void BaseDRAMSystem::PrintStats() {
    // Finish epoch output, remove last comma and append ]
    std::ofstream epoch_out(config_.json_epoch_name, std::ios_base::in |
                                                         std::ios_base::out |
                                                         std::ios_base::ate);
    epoch_out.seekp(-2, std::ios_base::cur);
    epoch_out.write("]", 1);
    epoch_out.close();

    std::ofstream json_out(config_.json_stats_name, std::ofstream::out);
    json_out << "{";

    // close it now so that each channel can handle it
    json_out.close();
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->PrintFinalStats();
        if (i != ctrls_.size() - 1) {
            std::ofstream chan_out(config_.json_stats_name, std::ofstream::app);
            chan_out << "," << std::endl;
        }
    }

/*    uint64_t read_cmds = 0, write_cmds = 0, read_row_hit = 0, write_row_hit = 0, read_latency = 0;
    for(size_t i = 0; i < ctrls_.size(); i++)
    {
        read_cmds += ctrls_[i]->ReturnReadCmds();
        write_cmds += ctrls_[i]->ReturnWriteCmds();
        read_row_hit += ctrls_[i]->ReturnReadRowHits();
        write_row_hit += ctrls_[i]->ReturnWriteRowHits();
        read_latency += ctrls_[i]->ReturnReadLatency();
    }
    fprintf(file, "FINAL STATS: ");
    fprintf(file, "Total read commands: %lu, write commands: %lu, read row hit: %lu, write row hit: %lu\n", read_cmds, write_cmds, read_row_hit, write_row_hit);
    fprintf(file, "DRAM active cycles: %lu, concurrent serve: %lu\n", active_cycles, concurrent_serve);
    fprintf(file, "Read row buffer hit rate: %f, write row buffer hit rate: %f\n", (float)read_row_hit/read_cmds, (float)write_row_hit/write_cmds);
    fprintf(file, "Row buffer hit rate: %f\n", (float)(read_row_hit + write_row_hit) / (read_cmds + write_cmds));
    fprintf(file, "Bank level parallelism: %f\n", (float)(concurrent_serve) / active_cycles);
*/
    json_out.open(config_.json_stats_name, std::ofstream::app);
    json_out << "}";

#ifdef THERMAL
    thermal_calc_.PrintFinalPT(clk_);
#endif  // THERMAL
}

void BaseDRAMSystem::ResetStats() {
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->ResetStats();
    }
}

void BaseDRAMSystem::RegisterCallbacks(
    std::function<void(uint64_t)> read_callback,
    std::function<void(uint64_t)> write_callback) {
    // TODO this should be propagated to controllers
    read_callback_ = read_callback;
    write_callback_ = write_callback;
}

JedecDRAMSystem::JedecDRAMSystem(Config &config, const std::string &output_dir,
                                 std::function<void(uint64_t)> read_callback,
                                 std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config, output_dir, read_callback, write_callback) {
    if (config_.IsHMC()) {
        std::cerr << "Initialized a memory system with an HMC config file!"
                  << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    ctrls_.reserve(config_.channels);
    for (auto i = 0; i < config_.channels; i++) {
#ifdef THERMAL
        ctrls_.push_back(new Controller(i, config_, timing_, thermal_calc_));
#else
        ctrls_.push_back(new Controller(i, config_, timing_));
#endif  // THERMAL
    }
}

JedecDRAMSystem::~JedecDRAMSystem() {
    for (auto it = ctrls_.begin(); it != ctrls_.end(); it++) {
        delete (*it);
    }
    fclose(file);
}

/*void JedecDRAMSystem::stats_mo()
{   
    uint64_t read_cmds = 0, write_cmds = 0, read_row_hit = 0, write_row_hit = 0;
    for(size_t i = 0; i < ctrls_.size(); i++)
    {   
        read_cmds += ctrls_[i]->ReturnReadCmds();
        write_cmds += ctrls_[i]->ReturnWriteCmds();
        read_row_hit += ctrls_[i]->ReturnReadRowHits();
        write_row_hit += ctrls_[i]->ReturnWriteRowHits();
    }
    uint64_t current_read_cmds = read_cmds - last_read_cmds;
    uint64_t current_write_cmds = write_cmds - last_write_cmds;
    uint64_t current_read_hit = read_row_hit - last_read_hits;
    uint64_t current_write_hit = write_row_hit - last_write_hits;
    fprintf(file, "[%lu-%lu]: ", clk_-5000, clk_);
    fprintf(file, "Total read commands: %lu, write commands: %lu, read row hit: %lu, write row hit: %lu\n", current_read_cmds, current_write_cmds, current_read_hit, current_write_hit);
    fprintf(file, "DRAM active cycles: %lu, concurrent serve: %lu\n", active_cycles-last_active_cycles, concurrent_serve-last_concurrent_serve);
    fprintf(file, "Read row buffer hit rate: %f, write row buffer hit rate: %f\n", (float)current_read_hit/current_read_cmds, (float)current_write_hit/current_write_cmds);
    fprintf(file, "Row buffer hit rate: %f\n", (float)(current_read_hit + current_write_hit) / (current_read_cmds + current_write_cmds));
    fprintf(file, "Bank level parallelism: %f\n", (float)(concurrent_serve-last_concurrent_serve) / (active_cycles-last_active_cycles));
    fprintf(file, "\n");
    last_read_cmds = read_cmds;
    last_write_cmds = write_cmds;
    last_read_hits = read_row_hit;
    last_write_hits = write_row_hit;
    last_concurrent_serve = concurrent_serve;
    last_active_cycles = active_cycles;
}
*/

bool JedecDRAMSystem::WillAcceptTransaction(uint64_t hex_addr,
                                            bool is_write) const {
    int channel = GetChannel(hex_addr);
    return ctrls_[channel]->WillAcceptTransaction(hex_addr, is_write);
}

bool JedecDRAMSystem::AddTransaction(uint64_t hex_addr, bool is_write) {
// Record trace - Record address trace for debugging or other purposes
#ifdef ADDR_TRACE
    address_trace_ << std::hex << hex_addr << std::dec << " "
                   << (is_write ? "WRITE " : "READ ") << clk_ << std::endl;
#endif

    int channel = GetChannel(hex_addr);
    bool ok = ctrls_[channel]->WillAcceptTransaction(hex_addr, is_write);

    assert(ok);
    if (ok) {
        Transaction trans = Transaction(hex_addr, is_write);
        ctrls_[channel]->AddTransaction(trans);
    }
    last_req_clk_ = clk_;
    return ok;
}

void JedecDRAMSystem::ClockTick() {
    //真正的时钟进行
    //依次对每一个memory controller进行操作
    for (size_t i = 0; i < ctrls_.size(); i++) {
        // look ahead and return earlier
        while (true) {
            //对每一个memory controller的read queue和write queue检查是否有在当前cycle（clk_）之前完成的transaction
            //也就是某个transaction.complete_cycle <= clk_
            //如果有，赋给pair
            auto pair = ctrls_[i]->ReturnDoneTrans(clk_);
            //pair.second代表is_write
            //pair.first代表addr
            //给回调函数write_callback或read_callback_
            if (pair.second == 1) {
                write_callback_(pair.first);
            } else if (pair.second == 0) {
		read_callback_(pair.first);
            } else {
                break;
            }
        }
    }
    //依次对每一个memory controller操作一遍
    for (size_t i = 0; i < ctrls_.size(); i++) {
        ctrls_[i]->ClockTick();
        //MZOU
        // 每个cycle依次检查每个controller，如果有一个是active，这个cycle就是active cycle(但是active cycles仅会加一次)
        // 这个cycle的concurrent_serve会叠加所有controller的返回值
        bool changed = 0;
        if(ctrls_[i]->ReturnIsActiveCycles())
        {
            if(!changed)
            {
                active_cycles += 1;
                changed = 1;
            }
            concurrent_serve += ctrls_[i]->ReturnConcurrentServe();
        }
        //MZOU
    }
    clk_++;

    if (clk_ % config_.epoch_period == 0) {
        PrintEpochStats();
    }
//    if (clk_ % 5000000 == 0)
//    {
//	stats_mo();
//    }
    return;
}

IdealDRAMSystem::IdealDRAMSystem(Config &config, const std::string &output_dir,
                                 std::function<void(uint64_t)> read_callback,
                                 std::function<void(uint64_t)> write_callback)
    : BaseDRAMSystem(config, output_dir, read_callback, write_callback),
      latency_(config_.ideal_memory_latency) {}

IdealDRAMSystem::~IdealDRAMSystem(){}

/*void IdealDRAMSystem::stats_mo()
{   
    uint64_t read_cmds = 0, write_cmds = 0, read_row_hit = 0, write_row_hit = 0;
    for(size_t i = 0; i < ctrls_.size(); i++)
    {   
        read_cmds += ctrls_[i]->ReturnReadCmds();
        write_cmds += ctrls_[i]->ReturnWriteCmds();
        read_row_hit += ctrls_[i]->ReturnReadRowHits();
        write_row_hit += ctrls_[i]->ReturnWriteRowHits();
    }
    uint64_t current_read_cmds = read_cmds - last_read_cmds;
    uint64_t current_write_cmds = write_cmds - last_write_cmds;
    uint64_t current_read_hit = read_row_hit - last_read_hits;
    uint64_t current_write_hit = write_row_hit - last_write_hits;
    fprintf(file, "[%lu-%lu]: ", clk_-5000, clk_);
    fprintf(file, "Total read commands: %lu, write commands: %lu, read row hit: %lu, write row hit: %lu\n", current_read_cmds, current_write_cmds, current_read_hit, current_write_hit);
    fprintf(file, "DRAM active cycles: %lu, concurrent serve: %lu\n", active_cycles-last_active_cycles, concurrent_serve-last_concurrent_serve);
    fprintf(file, "Read row buffer hit rate: %f, write row buffer hit rate: %f\n", (float)current_read_hit/current_read_cmds, (float)current_write_hit/current_write_cmds);
    fprintf(file, "Row buffer hit rate: %f\n", (float)(current_read_hit + current_write_hit) / (current_read_cmds + current_write_cmds));
    fprintf(file, "Bank level parallelism: %f\n", (float)(concurrent_serve-last_concurrent_serve) / (active_cycles-last_active_cycles));
    fprintf(file, "\n");
    last_read_cmds = read_cmds;
    last_write_cmds = write_cmds;
    last_read_hits = read_row_hit;
    last_write_hits = write_row_hit;
    last_concurrent_serve = concurrent_serve;
    last_active_cycles = active_cycles;
}
*/


bool IdealDRAMSystem::AddTransaction(uint64_t hex_addr, bool is_write) {
    auto trans = Transaction(hex_addr, is_write);
    trans.added_cycle = clk_;
    infinite_buffer_q_.push_back(trans);
    return true;
}

void IdealDRAMSystem::ClockTick() {
    for (auto trans_it = infinite_buffer_q_.begin();
         trans_it != infinite_buffer_q_.end();) {
        if (clk_ - trans_it->added_cycle >= static_cast<uint64_t>(latency_)) {
            if (trans_it->is_write) {
                write_callback_(trans_it->addr);
            } else {
                read_callback_(trans_it->addr);
            }
            trans_it = infinite_buffer_q_.erase(trans_it++);
        }
        if (trans_it != infinite_buffer_q_.end()) {
            ++trans_it;
        }
    }

    clk_++;
    return;
}

}  // namespace dramsim3
