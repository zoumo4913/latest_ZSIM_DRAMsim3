#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include <fstream>
#include <map>
#include <unordered_set>
#include <vector>
#include "channel_state.h"
#include "command_queue.h"
#include "common.h"
#include "refresh.h"
#include "simple_stats.h"

#ifdef THERMAL
#include "thermal.h"
#endif  // THERMAL

namespace dramsim3 {

enum class RowBufPolicy { OPEN_PAGE, CLOSE_PAGE, SIZE };

class Controller {
   public:
#ifdef THERMAL
    Controller(int channel, const Config &config, const Timing &timing,
               ThermalCalculator &thermalcalc);
#else
    Controller(int channel, const Config &config, const Timing &timing);
#endif  // THERMAL
    void ClockTick();
    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const;
    bool AddTransaction(Transaction trans);
    //MZOU
    // 统计parallelism相关
    void Calculate_stats();
    uint64_t ReturnConcurrentServe() const { return concurrent_serve; }
    bool ReturnIsActiveCycles(){ return is_active_cycles; }
    // 统计locality相关
    uint64_t ReturnReadCmds_Epoch() const { return read_cmds; }
    uint64_t ReturnWriteCmds_Epoch() const { return write_cmds; }
    uint64_t ReturnReadRowHits_Epoch() const { return read_row_hits; }
    uint64_t ReturnWriteRowHits_Epoch() const { return write_row_hits; }
    // 返回read latency
    uint64_t ReturnReadLatency() const { return simple_stats_.GetReadLatency(); }
    //MZOU
    int QueueUsage() const;
    // Stats output
    void PrintEpochStats();
    void PrintFinalStats();
    void ResetStats() { simple_stats_.Reset(); }
    std::pair<uint64_t, int> ReturnDoneTrans(uint64_t clock);

    int channel_id_;

   private:
    uint64_t clk_;
    const Config &config_;
    SimpleStats simple_stats_;
    ChannelState channel_state_;
    CommandQueue cmd_queue_;
    Refresh refresh_;
    //MZOU
    uint64_t concurrent_serve;
    bool is_active_cycles;
    uint64_t read_cmds;
    uint64_t write_cmds;
    uint64_t read_row_hits;
    uint64_t write_row_hits;
    //MZOU

#ifdef THERMAL
    ThermalCalculator &thermal_calc_;
#endif  // THERMAL

    // queue that takes transactions from CPU side
    bool is_unified_queue_;
    std::vector<Transaction> unified_queue_;
    std::vector<Transaction> read_queue_;
    std::vector<Transaction> write_buffer_;

    // transactions that are not completed, use map for convenience
    std::multimap<uint64_t, Transaction> pending_rd_q_;
    std::multimap<uint64_t, Transaction> pending_wr_q_;

    // completed transactions
    std::vector<Transaction> return_queue_;

    // row buffer policy
    RowBufPolicy row_buf_policy_;

#ifdef CMD_TRACE
    std::ofstream cmd_trace_;
#endif  // CMD_TRACE

    // used to calculate inter-arrival latency
    uint64_t last_trans_clk_;

    // transaction queueing
    int write_draining_;
    void ScheduleTransaction();
    void IssueCommand(const Command &tmp_cmd);
    Command TransToCommand(const Transaction &trans);
    void UpdateCommandStats(const Command &cmd, int count);
};
}  // namespace dramsim3
#endif
