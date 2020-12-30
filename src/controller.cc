#include "controller.h"
#include <iomanip>
#include <iostream>
#include <limits>

namespace dramsim3 {

#ifdef THERMAL
Controller::Controller(int channel, const Config &config, const Timing &timing,
                       ThermalCalculator &thermal_calc)
#else
Controller::Controller(int channel, const Config &config, const Timing &timing)
#endif  // THERMAL
    : channel_id_(channel),
      clk_(0),
      config_(config),
      simple_stats_(config_, channel_id_),
      channel_state_(config, timing),
      cmd_queue_(channel_id_, config, channel_state_, simple_stats_),
      refresh_(config, channel_state_),
      //MZOU
      concurrent_serve(0),
      is_active_cycles(0),
      read_cmds(0),
      write_cmds(0),
      read_row_hits(0),
      write_row_hits(0),
      //MZOU
#ifdef THERMAL
      thermal_calc_(thermal_calc),
#endif  // THERMAL
      is_unified_queue_(config.unified_queue),
      row_buf_policy_(config.row_buf_policy == "CLOSE_PAGE"
                          ? RowBufPolicy::CLOSE_PAGE
                          : RowBufPolicy::OPEN_PAGE),
      last_trans_clk_(0),
      write_draining_(0) {
    if (is_unified_queue_) {
        //read 和 write transaction都在一个queue里
        unified_queue_.reserve(config_.trans_queue_size);
    } else {
        read_queue_.reserve(config_.trans_queue_size);
        write_buffer_.reserve(config_.trans_queue_size);
    }

#ifdef CMD_TRACE
    std::string trace_file_name = config_.output_prefix + "ch_" +
                                  std::to_string(channel_id_) + "cmd.trace";
    std::cout << "Command Trace write to " << trace_file_name << std::endl;
    cmd_trace_.open(trace_file_name, std::ofstream::out);
#endif  // CMD_TRACE
}

//所有已经完成的transaction都会被加入return_queue
//每个cycle都会在return_queue里检查，是否有read或write的完成时间小于当前cycle，如果有，说明可以返回给cpu了
//将这样的transaction都构造成pair的格式，pair.first = addr, pair.second = is_write
std::pair<uint64_t, int> Controller::ReturnDoneTrans(uint64_t clk) {
    auto it = return_queue_.begin();
    while (it != return_queue_.end()) {
        if (clk >= it->complete_cycle) {
            if (it->is_write) {
                simple_stats_.Increment("num_writes_done");
            } else {
                simple_stats_.Increment("num_reads_done");
                simple_stats_.AddValue("read_latency", clk_ - it->added_cycle);
            }
            auto pair = std::make_pair(it->addr, it->is_write);
            it = return_queue_.erase(it);
            return pair;
        } else {
            ++it;
        }
    }
    return std::make_pair(-1, -1);
}

//对于memory controller来说，每个cycle里要做的事情：
//1. 判断是否需要刷新
//2. 在command_queue里拿出一个command并开始执行
//3. 统计状态并执行command
//4. 调度下一个transaction
//5. 出发command_queue的时钟更新
void Controller::ClockTick() {
    // update refresh counter
    //先更新refresh，如果当前cycle需要refresh，那么所有状态要被保存并停止
    refresh_.ClockTick();

    bool cmd_issued = false;
    //command包括 command_type，addr和hex_addr
    Command cmd;
    if (channel_state_.IsRefreshWaiting()) {
        cmd = cmd_queue_.FinishRefresh();
    }

    // cannot find a refresh related command or there's no refresh
    // 如果不需要刷新
    //首先，cmd_queue_里应该是只有read/write的，是解析自transaction的，GetCommadnToIssue函数通过遍历cmd_queue_（相当于先来先服务），对下一条command分析其前置指令（如activate等）
    //如果符合时序，这里的cmd返回的可能是read/write(所有的前置指令都执行完毕，row buffer命中，可以去执行真正的read/write了)
    //也可能是activate或precharge等其他命令
    if (!cmd.IsValid()) {
        cmd = cmd_queue_.GetCommandToIssue();
    }

    if (cmd.IsValid()) {
        IssueCommand(cmd);
        cmd_issued = true;

        if (config_.enable_hbm_dual_cmd) {
            auto second_cmd = cmd_queue_.GetCommandToIssue();
            if (second_cmd.IsValid()) {
                if (second_cmd.IsReadWrite() != cmd.IsReadWrite()) {
                    IssueCommand(second_cmd);
                    simple_stats_.Increment("hbm_dual_cmds");
                }
            }
        }
    }

    // power updates pt 1
    //统计当前cycle的channel状态
    //对每一个rank检查，如果rank在refreshing，sref_cycles + 1
    //如果该rank的所有bank都是空闲的，all_bank_idle_cycles + 1
    //如果有bank不是空闲的，也就是row_open状态，那么这个rank就是active的，rank_active_cycles + 1
    //此处可以用于统计当前cycle的当前rank有多少个bank在工作
    for (int i = 0; i < config_.ranks; i++) {
        if (channel_state_.IsRankSelfRefreshing(i)) {
            simple_stats_.IncrementVec("sref_cycles", i);
        } else {
            bool all_idle = channel_state_.IsAllBankIdleInRank(i);
            if (all_idle) {
                simple_stats_.IncrementVec("all_bank_idle_cycles", i);
                channel_state_.rank_idle_cycles[i] += 1;
            } else {
                simple_stats_.IncrementVec("rank_active_cycles", i);
                // reset
                channel_state_.rank_idle_cycles[i] = 0;
            }
        }
    }

    // power updates pt 2: move idle ranks into self-refresh mode to save power
    //只有当允许rank自己刷新的时候，才有效
    if (config_.enable_self_refresh && !cmd_issued) {
        
        for (auto i = 0; i < config_.ranks; i++) {
            if (channel_state_.IsRankSelfRefreshing(i)) {
                // wake up!
                if (!cmd_queue_.rank_q_empty[i]) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_EXIT, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            } else {
                if (cmd_queue_.rank_q_empty[i] &&
                    channel_state_.rank_idle_cycles[i] >=
                        config_.sref_threshold) {
                    auto addr = Address();
                    addr.rank = i;
                    auto cmd = Command(CommandType::SREF_ENTER, addr, -1);
                    cmd = channel_state_.GetReadyCommand(cmd, clk_);
                    if (cmd.IsValid()) {
                        IssueCommand(cmd);
                        break;
                    }
                }
            }
        }
    }

    //MZOU
    Calculate_stats();
    //MZOU
    ScheduleTransaction();
    clk_++;
    cmd_queue_.ClockTick();
    simple_stats_.Increment("num_cycles");
    return;
}

//MZOU
//统计这个cycle的状态
void Controller::Calculate_stats()
{
    is_active_cycles = 0;
    concurrent_serve = 0;
    for(int i = 0; i < config_.ranks; i++)
    {
        for(int j = 0; j < config_.bankgroups; j++)
        {
            for(int k = 0; k < config_.banks_per_group; k++)
            {
                if(clk_ == channel_state_.GetServeEndCycleBank(i, j, k)+1)
                {
                    channel_state_.SetInServeBank(i, j, k, false);
                }
                if(channel_state_.IsRowClosed(i, j, k))
                {
                    channel_state_.SetInServeBank(i, j, k, false);
                }
            }
        }
        if(channel_state_.InServeBankNum(i) > 0)
        {
            is_active_cycles = 1;
            concurrent_serve += channel_state_.InServeBankNum(i);
        }
    }
}

//由memory controller决定是否接受下一个来自trace_file的transaction
//就是看queue里还有没有容量
bool Controller::WillAcceptTransaction(uint64_t hex_addr, bool is_write) const {
    if (is_unified_queue_) {
        return unified_queue_.size() < unified_queue_.capacity();
    } else if (!is_write) {
//	std::cout << "This is a read request, read_queue.size = " << read_queue_.size() << ", capacity = " << read_queue_.capacity() << std::endl;
        return read_queue_.size() < read_queue_.capacity();
    } else {
//	std::cout << "This is a write request, write_buffer.size = " << write_buffer_.size() << ", capacity = " << write_buffer_.capacity() << std::endl;
        return write_buffer_.size() < write_buffer_.capacity();
    }
}

//来自cpu的transaction会保存在read queue / write buffer里， 而pending_rd_q_和pending_wr_q_里保存的是还未被翻译成command的transaction
//pending_rd_q_和pending_wr_q_分别缓存了未被翻译的read/write transaction
//翻译完成后，会将pending里的相应内容删除，转而保存在cmd_queue里
bool Controller::AddTransaction(Transaction trans) {
    //trans加入到queue里的时刻是当前cycle
    //last_trans_clk是上一个trans加入到queue里的时刻，interarrival_latency是相邻两个transaction加入到queue里的时间
    trans.added_cycle = clk_;
    simple_stats_.AddValue("interarrival_latency", clk_ - last_trans_clk_);
    last_trans_clk_ = clk_;

    if (trans.is_write) {
        //pending_wr_q_是一个write buffer，缓存了所有要处理但还未被翻译成command的transaction，可以给read命令提供旁路，也可以合并多个对同一地址的写入命令
        //如果count == 0，说明在pending_wr_q_里没有对这一地址的写入，需要新加进去
        if (pending_wr_q_.count(trans.addr) == 0) {  // can not merge writes
            //将transaction加入dram的待翻译队列中，等待被调度翻译成command
            pending_wr_q_.insert(std::make_pair(trans.addr, trans));
            if (is_unified_queue_) {
                unified_queue_.push_back(trans);
            } else {
                write_buffer_.push_back(trans);
            }
        }
        //加入queue里这个write trans就完成了，剩下的是dram完成的工作，所以设置trans的complete_cycle并加入return_queue里
        trans.complete_cycle = clk_ + 1;
        return_queue_.push_back(trans);
        return true;
    } else {  // read
        // if in write buffer, use the write buffer value
        //如果是read transaction，先检查pending_wr_q_是否有对于同一地址的写回，如果有，这个read已经完成了，可以被加入return_queue_里
        if (pending_wr_q_.count(trans.addr) > 0) {
            trans.complete_cycle = clk_ + 1;
            return_queue_.push_back(trans);
            return true;
        }
        //如果pending_wr_q_里没有旁路，只能加入到pending_rd_q_，等待被调度解析成command
        pending_rd_q_.insert(std::make_pair(trans.addr, trans));
        if (pending_rd_q_.count(trans.addr) == 1) {
            if (is_unified_queue_) {
                unified_queue_.push_back(trans);
            } else {
                read_queue_.push_back(trans);
            }
        }
        return true;
    }
}

//对于pending_wr_q_和pending_rd_q_的transaction调度，决定下一个处理的transaction
void Controller::ScheduleTransaction() {
    // determine whether to schedule read or write
    //先来决定读还是写，只有当write_buffer_到达一定阈值，才会设置write_draining为write_buffer_.size（），这个cycle去写，否则调度读
    //虽然后面的cycle里write_queue_不断增加，但write_draining会保持在这一时刻的大小
    if (write_draining_ == 0 && !is_unified_queue_) {
        // we basically have a upper and lower threshold for write buffer
        if ((write_buffer_.size() >= write_buffer_.capacity()) ||
            (write_buffer_.size() > 8 && cmd_queue_.QueueEmpty())) {
            write_draining_ = write_buffer_.size();
        }
    }

    //queue会根据write_draining指向write_buffer_或read_queue_
    //从queue的第一个开始遍历，直到找到一个对应的（rank，bank）可以接收的transaction
    //所以当转换到write模式以后，只有两个转机会回到read：在pending_rd_q_里找到相应的命令，也就是旁路上去；或者所有在触发时刻的write_buffer_都处理完了
    //所以write_buffer和read_queue是对于cpu的，当将这个transaction翻译完成就可以在write_queue或read_queue里删除了，但是pending_rd_q和pending_wr_q里的内容则是针对dram的，只有当真正处理完才能够删除
    std::vector<Transaction> &queue = is_unified_queue_ ? unified_queue_ : write_draining_ > 0 ? write_buffer_ : read_queue_;
    for (auto it = queue.begin(); it != queue.end(); it++) {
        auto cmd = TransToCommand(*it);
        if (cmd_queue_.WillAcceptCommand(cmd.Rank(), cmd.Bankgroup(), cmd.Bank())) {
            if (!is_unified_queue_ && cmd.IsWrite()) {
                // Enforce R->W dependency
                if (pending_rd_q_.count(it->addr) > 0) {
                    write_draining_ = 0;
                    break;
                }
                write_draining_ -= 1;
            }
            cmd_queue_.AddCommand(cmd);
            queue.erase(it);
            break;
        }
    }
}

//memory controller处理command
//如果这个command是read或write，直接处理
//如果是其他的，更新bank状态，相当于处理了这个refresh/activate/precharge命令
void Controller::IssueCommand(const Command &cmd) {
    // std::cout << "rank: " << cmd.Rank() << ", bankgroup: " << cmd.Bankgroup() << ", bank: " << cmd.Bank() << ", row: " << cmd.Row() << ", column: " << cmd.Column() << std::endl; 
#ifdef CMD_TRACE
    cmd_trace_ << std::left << std::setw(18) << clk_ << " " << cmd << std::endl;
#endif  // CMD_TRACE
#ifdef THERMAL
    // add channel in, only needed by thermal module
    thermal_calc_.UpdateCMDPower(channel_id_, cmd, clk_);
#endif  // THERMAL
    // if read/write, update pending queue and return queue
    //对于read command，先检查pending_rd_q_里有多少个对相同地址的read
    int num_count = 0;
    if (cmd.IsRead()) {
        auto num_reads = pending_rd_q_.count(cmd.hex_addr);
	num_count = num_reads;
        //错误情况
        if (num_reads == 0) {
            std::cerr << cmd.hex_addr << " not in read queue! " << std::endl;
            exit(1);
        }
        // if there are multiple reads pending return them all
        // complete_cycle是当前cycle加上read_delay，当前cycle已经被dram里的各种操作影响过
        while (num_reads > 0) {
            auto it = pending_rd_q_.find(cmd.hex_addr);
            it->second.complete_cycle = clk_ + config_.read_delay;
            return_queue_.push_back(it->second);
            pending_rd_q_.erase(it);
            num_reads -= 1;
        }
    } else if (cmd.IsWrite()) {
        // there should be only 1 write to the same location at a time
        auto it = pending_wr_q_.find(cmd.hex_addr);
        if (it == pending_wr_q_.end()) {
            std::cerr << cmd.hex_addr << " not in write queue!" << std::endl;
            exit(1);
        }
	num_count = 1;
        auto wr_lat = clk_ - it->second.added_cycle + config_.write_delay;
        simple_stats_.AddValue("write_latency", wr_lat);
        pending_wr_q_.erase(it);
    }
    // must update stats before states (for row hits)
    //UpdataCommandStats这个函数根据cmd的类型，更新计数器（simple_stats_）
    UpdateCommandStats(cmd, num_count);
    //UpdateTimingAndStates这个函数包括两部分，cmd会落在某个（channel，rank，bank）
    //首先根据bank当前的状态与cmd的具体要求，更改bank的下一时刻状态，比如当前bank是closed，cmd是activate，那么会将bank的状态改为open，且open_row = cmd.row。相当于执行了这个cmd
    //然后更新时序
    channel_state_.UpdateTimingAndStates(cmd, clk_);

    //MZOU
    if(cmd.IsRead())
    {
        //std::cout << "rank " << cmd.Rank() << ", bankgroup " << cmd.Bankgroup() << ", bank " << cmd.Bank() << " starts to serve at " << clk_ << ", by READ, end at " << clk_+config_.read_delay << std::endl;
        channel_state_.SetInServeBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), true);
        channel_state_.SetServeEndCycleBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), clk_+config_.read_delay);
    }
    // else if(cmd.IsWrite())
    // {
    //     channel_state_.SetInServeBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), true);
    //     channel_state_.SetServeEndCycleBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), clk_+config_.write_delay);
    // }
    else if(cmd.cmd_type == CommandType::ACTIVATE)
    {  
        // 如果返回的是0，说明这个activate是被read激活的
        if(!channel_state_.GetActivateByWhoBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank()))
        {
            //std::cout << "This activate is by read" << std::endl;
            //std::cout << "rank " << cmd.Rank() << ", bankgroup " << cmd.Bankgroup() << ", bank " << cmd.Bank() << " starts to serve at " << clk_ << ", by ACTIVATE" << std::endl;
            channel_state_.SetInServeBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), true);
        }
        // 如果返回的是1，说明这个activate是被write激活的
        // else
        // {
        //     std::cout << "This activate is by write" << std::endl;
        // }
    }
    else if(cmd.cmd_type == CommandType::PRECHARGE)
    {
         // 如果返回的是1，说明这个precharge是被activate触发的
        if(channel_state_.GetPrechargeByRefreshBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank()))
        {
    //         //std::cout << "This precharge is by activate" << std::endl;
    //         //std::cout << "rank " << cmd.Rank() << ", bankgroup " << cmd.Bankgroup() << ", bank " << cmd.Bank() << " starts to serve at " << clk_ << ", by PRECHARGE" << std::endl;
            channel_state_.SetInServeBank(cmd.Rank(), cmd.Bankgroup(), cmd.Bank(), true);
        }
    //     // 如果返回的是0，说明这个precharge是被refresh触发的
    //     // else
    //     // {
    //     //     std::cout << "This precharge is by refresh" << std::endl;
    //     // }
    }
    //MZOU
}

//将一个transaction解析成command
//最终的command包括type（read/write），这个command要去往的（channel，rank，bank group，bank，row，column）和transaction要读取/写入的十六进制地址（来自trace文件）
Command Controller::TransToCommand(const Transaction &trans) {
    auto addr = config_.AddressMapping(trans.addr);
    CommandType cmd_type;
    if (row_buf_policy_ == RowBufPolicy::OPEN_PAGE) {
        cmd_type = trans.is_write ? CommandType::WRITE : CommandType::READ;
    } else {
        cmd_type = trans.is_write ? CommandType::WRITE_PRECHARGE
                                  : CommandType::READ_PRECHARGE;
    }
    return Command(cmd_type, addr, trans.addr);
}


int Controller::QueueUsage() const { return cmd_queue_.QueueUsage(); }

void Controller::PrintEpochStats() {
    simple_stats_.Increment("epoch_num");
    simple_stats_.PrintEpochStats();
#ifdef THERMAL
    for (int r = 0; r < config_.ranks; r++) {
        double bg_energy = simple_stats_.RankBackgroundEnergy(r);
        thermal_calc_.UpdateBackgroundEnergy(channel_id_, r, bg_energy);
    }
#endif  // THERMAL
    return;
}

void Controller::PrintFinalStats() {
    simple_stats_.PrintFinalStats();

#ifdef THERMAL
    for (int r = 0; r < config_.ranks; r++) {
        double bg_energy = simple_stats_.RankBackgroundEnergy(r);
        thermal_calc_.UpdateBackgroundEnergy(channel_id_, r, bg_energy);
    }
#endif  // THERMAL
    return;
}

//在这里统计row buffer hit && miss
void Controller::UpdateCommandStats(const Command &cmd, int count) {
    switch (cmd.cmd_type) {
        case CommandType::READ:
        case CommandType::READ_PRECHARGE:
            simple_stats_.Increment("num_read_cmds");
            read_cmds += count;
	    if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_read_row_hits");
            	read_row_hits += count;
	    }
            break;
        case CommandType::WRITE:
        case CommandType::WRITE_PRECHARGE:
            simple_stats_.Increment("num_write_cmds");
	    write_cmds += count;
            if (channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(),
                                           cmd.Bank()) != 0) {
                simple_stats_.Increment("num_write_row_hits");
            	write_row_hits += count;
	    }
            break;
        case CommandType::ACTIVATE:
            simple_stats_.Increment("num_act_cmds");
            break;
        case CommandType::PRECHARGE:
            simple_stats_.Increment("num_pre_cmds");
            break;
        case CommandType::REFRESH:
            simple_stats_.Increment("num_ref_cmds");
            break;
        case CommandType::REFRESH_BANK:
            simple_stats_.Increment("num_refb_cmds");
            break;
        case CommandType::SREF_ENTER:
            simple_stats_.Increment("num_srefe_cmds");
            break;
        case CommandType::SREF_EXIT:
            simple_stats_.Increment("num_srefx_cmds");
            break;
        default:
            AbruptExit(__FILE__, __LINE__);
    }
}

}  // namespace dramsim3
