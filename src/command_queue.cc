#include "command_queue.h"

namespace dramsim3 {

CommandQueue::CommandQueue(int channel_id, const Config& config,
                           ChannelState& channel_state,
                           SimpleStats& simple_stats)
    : rank_q_empty(config.ranks, true),
      config_(config),
      channel_state_(channel_state),
      simple_stats_(simple_stats),
      is_in_ref_(false),
      queue_size_(static_cast<size_t>(config_.cmd_queue_size)),
      queue_idx_(0),
      clk_(0) {
        //设置command_queue单位，每个bank还是每个rank拥有一个command queue
        if (config_.queue_structure == "PER_BANK") {
            queue_structure_ = QueueStructure::PER_BANK;
            num_queues_ = config_.banks * config_.ranks;
        } else if (config_.queue_structure == "PER_RANK") {
            queue_structure_ = QueueStructure::PER_RANK;
            num_queues_ = config_.ranks;
        } else {
            std::cerr << "Unsupportted queueing structure "
                  << config_.queue_structure << std::endl;
            AbruptExit(__FILE__, __LINE__);
        }

        queues_.reserve(num_queues_);
        for (int i = 0; i < num_queues_; i++) {
            auto cmd_queue = std::vector<Command>();
            cmd_queue.reserve(config_.cmd_queue_size);
            queues_.push_back(cmd_queue);
        }
      }

Command CommandQueue::GetCommandToIssue() {
    for (int i = 0; i < num_queues_; i++) {
        auto& queue = GetNextQueue();
        // if we're refresing, skip the command queues that are involved
        //如果当前queue所在的bank正在被刷新，跳过
        if (is_in_ref_) {
            if (ref_q_indices_.find(queue_idx_) != ref_q_indices_.end()) {
                continue;
            }
        }
        
        //遍历每一个command_queue（可能是bank为单位也可能是rank为单位）找到这个queue里的read/write command的前置cmd
        //只有一种情况前置cmd会是read或write：即所处bank的row是open状态且openRow==command.row，那么cmd.type = command.type，也就是read或write
        //说明这个read/write可以被执行了，所以在command_queue里被删除
        auto cmd = GetFirstReadyInQueue(queue);
        if (cmd.IsValid()) {
            if (cmd.IsReadWrite()) {
                EraseRWCommand(cmd);
            }
            return cmd;
        }
    }
    return Command();
}

Command CommandQueue::FinishRefresh() {
    // we can do something fancy here like clearing the R/Ws
    // that already had ACT on the way but by doing that we
    // significantly pushes back the timing for a refresh
    // so we simply implement an ASAP approach
    auto ref = channel_state_.PendingRefCommand();
    if (!is_in_ref_) {
        GetRefQIndices(ref);
        is_in_ref_ = true;
    }

    // either precharge or refresh
    auto cmd = channel_state_.GetReadyCommand(ref, clk_);

    if (cmd.IsRefresh()) {
        ref_q_indices_.clear();
        is_in_ref_ = false;
    }
    return cmd;
}

bool CommandQueue::ArbitratePrecharge(const CMDIterator& cmd_it,
                                      const CMDQueue& queue) const {
    auto cmd = *cmd_it;

    for (auto prev_itr = queue.begin(); prev_itr != cmd_it; prev_itr++) {
        if (prev_itr->Rank() == cmd.Rank() &&
            prev_itr->Bankgroup() == cmd.Bankgroup() &&
            prev_itr->Bank() == cmd.Bank()) {
            return false;
        }
    }

    bool pending_row_hits_exist = false;
    int open_row =
        channel_state_.OpenRow(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto pending_itr = cmd_it; pending_itr != queue.end(); pending_itr++) {
        if (pending_itr->Row() == open_row &&
            pending_itr->Bank() == cmd.Bank() &&
            pending_itr->Bankgroup() == cmd.Bankgroup() &&
            pending_itr->Rank() == cmd.Rank()) {
            pending_row_hits_exist = true;
            break;
        }
    }

    bool rowhit_limit_reached =
        channel_state_.RowHitCount(cmd.Rank(), cmd.Bankgroup(), cmd.Bank()) >=
        4;
    if (!pending_row_hits_exist || rowhit_limit_reached) {
        simple_stats_.Increment("num_ondemand_pres");
        return true;
    }
    return false;
}

bool CommandQueue::WillAcceptCommand(int rank, int bankgroup, int bank) const {
    int q_idx = GetQueueIndex(rank, bankgroup, bank);
    return queues_[q_idx].size() < queue_size_;
}

bool CommandQueue::QueueEmpty() const {
    for (const auto q : queues_) {
        if (!q.empty()) {
            return false;
        }
    }
    return true;
}

// 每一个bank都有自己的command queue
bool CommandQueue::AddCommand(Command cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    if (queue.size() < queue_size_) {
        queue.push_back(cmd);
        rank_q_empty[cmd.Rank()] = false;
        return true;
    } else {
        return false;
    }
}

CMDQueue& CommandQueue::GetNextQueue() {
    queue_idx_++;
    if (queue_idx_ == num_queues_) {
        queue_idx_ = 0;
    }
    return queues_[queue_idx_];
}

//refresh相关
void CommandQueue::GetRefQIndices(const Command& ref) {
    if (ref.cmd_type == CommandType::REFRESH) {
        if (queue_structure_ == QueueStructure::PER_BANK) {
            for (int i = 0; i < num_queues_; i++) {
                if (i / config_.banks == ref.Rank()) {
                    ref_q_indices_.insert(i);
                }
            }
        } else {
            ref_q_indices_.insert(ref.Rank());
        }
    } else {  // refb
        int idx = GetQueueIndex(ref.Rank(), ref.Bankgroup(), ref.Bank());
        ref_q_indices_.insert(idx);
    }
    return;
}

int CommandQueue::GetQueueIndex(int rank, int bankgroup, int bank) const {
    if (queue_structure_ == QueueStructure::PER_RANK) {
        return rank;
    } else {
        return rank * config_.banks + bankgroup * config_.banks_per_group +
               bank;
    }
}

CMDQueue& CommandQueue::GetQueue(int rank, int bankgroup, int bank) {
    int index = GetQueueIndex(rank, bankgroup, bank);
    return queues_[index];
}

// MZOU
// FR-FCFS原则是通过时序控制实现的
// 由于对同一个bank的read-read-latency < read-precharge-latenct，所以即使每次都是顺序遍历command queue，也会优先执行对同一个bank命中的command
// 比如cmd1会产生row buffer miss，cmd2会产生row buffer hit
// 在某一个cycle结束了上一个cmd的执行，此时由于时序限制，对cmd1的GetReadyCommand返回的是个无效指令，对cmd2的GetReadyCommand已经可以返回read command了
// 所以即使cmd2在queue里是后遍历到的，也会优先执行cmd2
// 以此达到FR-FCFS的原则
Command CommandQueue::GetFirstReadyInQueue(CMDQueue& queue) const {
    //std::cout << "queue.size in clk " << clk_ << " is " << queue.size() << std::endl;
    //对于command queue里的所有指令，都是来自addCommandQueue，也就是解析自transaction，只有read或write两种
    //所以对每一个command，都要去设置他的前置指令（如activate等）
    //依次将queue里的所有command送至GetReadyCommand函数里，返回的那个cmd应该是在执行下一个command（read或write）之前需要被执行的cmd（如activate）
    for(auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++)
    {
        Command cmd = channel_state_.GetReadyCommand(*cmd_it, clk_);
        //如果某一个command的前置指令由于时序问题还不能开始执行，那么去判断下一个command
        if (!cmd.IsValid()) {
            continue;
        }
        if (cmd.cmd_type == CommandType::PRECHARGE) {
            if (!ArbitratePrecharge(cmd_it, queue)) {
                continue;
            }
        } else if (cmd.IsWrite()) {
            if (HasRWDependency(cmd_it, queue)) {
                continue;
            }
        }
        return cmd;
    }

    return Command();
}

void CommandQueue::EraseRWCommand(const Command& cmd) {
    auto& queue = GetQueue(cmd.Rank(), cmd.Bankgroup(), cmd.Bank());
    for (auto cmd_it = queue.begin(); cmd_it != queue.end(); cmd_it++) {
        if (cmd.hex_addr == cmd_it->hex_addr && cmd.cmd_type == cmd_it->cmd_type) {
            queue.erase(cmd_it);
            return;
        }
    }
    std::cerr << "cannot find cmd!" << std::endl;
    exit(1);
}

int CommandQueue::QueueUsage() const {
    int usage = 0;
    for (auto i = queues_.begin(); i != queues_.end(); i++) {
        usage += i->size();
    }
    return usage;
}

bool CommandQueue::HasRWDependency(const CMDIterator& cmd_it,
                                   const CMDQueue& queue) const {
    // Read after write has been checked in controller so we only
    // check write after read here
    for (auto it = queue.begin(); it != cmd_it; it++) {
        if (it->IsRead() && it->Row() == cmd_it->Row() &&
            it->Column() == cmd_it->Column() && it->Bank() == cmd_it->Bank() &&
            it->Bankgroup() == cmd_it->Bankgroup()) {
            return true;
        }
    }
    return false;
}

}  // namespace dramsim3
