#ifndef __SIMPLE_STATS_
#define __SIMPLE_STATS_

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "configuration.h"
#include "json.hpp"

namespace dramsim3 {

class SimpleStats {
   public:
    SimpleStats(const Config& config, int channel_id);
    // incrementing counter
    void Increment(const std::string name) { epoch_counters_[name] += 1; }

    // incrementing for vec counter
    void IncrementVec(const std::string name, int pos) {
        epoch_vec_counters_[name][pos] += 1;
    }

    // increment vec counter by number
    void IncrementVecBy(const std::string name, int pos, int num) {
        epoch_vec_counters_[name][pos] += num;
    }

    // add historgram value
    void AddValue(const std::string name, const int value);

    // Epoch update
    void PrintEpochStats();

    // Final statas output
    void PrintFinalStats();

    // Reset (usually after one phase of simulation)
    void Reset();

    //MZOU
    // 返回read_cmds, write_cmds, read_row_hits, write_row_hits
    uint64_t GetReadCmds() const;
    uint64_t GetWriteCmds() const;
    uint64_t GetReadRowHits() const;
    uint64_t GetWriteRowHits() const;
    // 返回read latency
    uint64_t GetReadLatency() const;
    // 返回read cmds(应该就是read_cmds)
    uint64_t GetReadCmdsTwo() const;
    //MZOU

   private:
    using VecStat = std::unordered_map<std::string, std::vector<uint64_t> >;
    using HistoCount = std::unordered_map<int, uint64_t>;
    using Json = nlohmann::json;
    void InitStat(std::string name, std::string stat_type,
                  std::string description);
    void InitVecStat(std::string name, std::string stat_type,
                     std::string description, std::string part_name,
                     int vec_len);
    void InitHistoStat(std::string name, std::string description, int start_val,
                       int end_val, int num_bins);

    void UpdateCounters();
    void UpdateHistoBins();
    void UpdatePrints(bool epoch);
    double GetHistoAvg(const HistoCount& histo_counts) const;
    std::string GetTextHeader(bool is_final) const;
    void UpdateEpochStats();
    void UpdateFinalStats();

    const Config& config_;
    int channel_id_;

    // map names to descriptions
    std::unordered_map<std::string, std::string> header_descs_;

    // counter stats, indexed by their name
    std::unordered_map<std::string, uint64_t> counters_;
    std::unordered_map<std::string, uint64_t> epoch_counters_;

    // vectored counter stats, first indexed by name then by index
    VecStat vec_counters_;
    VecStat epoch_vec_counters_;

    // NOTE: doubles_ vec_doubles_ and calculated_ are basically one time
    // placeholders after each epoch they store the value for that epoch
    // (different from the counters) and in the end updated to the overall value
    std::unordered_map<std::string, double> doubles_;

    std::unordered_map<std::string, std::vector<double> > vec_doubles_;

    // calculated stats, similar to double, but not the same
    std::unordered_map<std::string, double> calculated_;

    // histogram stats
    std::unordered_map<std::string, std::vector<std::string> > histo_headers_;

    std::unordered_map<std::string, std::pair<int, int> > histo_bounds_;
    std::unordered_map<std::string, int> bin_widths_;
    // MZOU
    // histo_counts记录的是整体历史水平，epoch_histo_counts记录的是一段时间的信息
    // 其中第一个string代表的是不同数组，如read_latency, arrive_interval等，使用histo_counts[name]就能定位到想要的历史信息
    // 第二个HistoCount是一个结构，即 unordered map<int, uint64_t>,第一个int反映的是latcncy，第二个uint64_t反映的是有多少个access是这样的latency
    // 比如 histo_counts[read_latency]有一个对象是<25, 4>，说明有4个access的read latency是25个cycle
    // MZOU
    std::unordered_map<std::string, HistoCount> histo_counts_;
    std::unordered_map<std::string, HistoCount> epoch_histo_counts_;
    VecStat histo_bins_;
    VecStat epoch_histo_bins_;

    // outputs
    Json j_data_;
    std::vector<std::pair<std::string, std::string> > print_pairs_;
};

}  // namespace dramsim3
#endif