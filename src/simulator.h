// simulator.h - the whole system: cores, scheduler, MMU/TLBs, caches, memory.
//
//   +--------- core 0 ---------+   +--------- core 1 ---------+
//   | running process          |   | running process          |
//   | TLB (per core)           |   | TLB (per core)           |
//   | L1 cache (per core)      |   | L1 cache (per core)      |
//   +------------+-------------+   +------------+-------------+
//                +-------------+---------------+
//                         shared L2 cache
//                              |
//                     DRAM: physical frames  <-- page tables, replacement policy
//
// Time advances in ticks. Each core executes one memory access of its process
// and is then busy for as many ticks as that access costs.
#pragma once

#include <memory>
#include <ostream>
#include <vector>

#include "cache.h"
#include "config.h"
#include "memory.h"
#include "process.h"
#include "scheduler.h"
#include "tlb.h"

struct GanttSegment {
	int core;
	int pid;
	uint64_t start, end;
};

struct SimStats {
	uint64_t makespan = 0;          // tick when the last process finished
	uint64_t context_switches = 0;
	uint64_t preemptions = 0;
	uint64_t accesses = 0;          // memory accesses that completed normally
	uint64_t memory_ticks = 0;      // ticks those accesses cost
	uint64_t tlb_hits = 0, tlb_misses = 0, tlb_flushes = 0, tlb_shootdowns = 0;
	uint64_t l1_hits = 0, l1_misses = 0, l2_hits = 0, l2_misses = 0;
	uint64_t page_faults = 0, evictions = 0;
	std::vector<double> utilization; // per core, 0..1

	double tlb_hit_rate() const;
	double l1_hit_rate() const;
	double l2_hit_rate() const;
	double amat() const;             // average memory access time, ticks
	double avg_wait = 0, avg_turnaround = 0, avg_response = 0;
};

class Simulator {
public:
	Simulator(const Config &cfg, std::vector<Process> procs);

	void run();
	const SimStats &stats() const { return stats_; }
	const std::vector<Process> &processes() const { return procs_; }
	const std::vector<GanttSegment> &gantt() const { return gantt_; }

	void print_report(std::ostream &out) const;
	void print_gantt(std::ostream &out, size_t max_segments = 40) const;
	static void print_csv_header(std::ostream &out);
	void print_csv(std::ostream &out) const;

private:
	struct Core {
		Process *running = nullptr;
		uint64_t busy_until = 0;
		uint64_t busy_ticks = 0;
		int last_pid = -1;          // whose TLB entries are loaded
		Tlb tlb;
		Cache l1;
		Core(const Config &c);
	};

	void admit_and_wake(uint64_t now);
	void step_core(int id, uint64_t now);
	uint64_t memory_access(Core &core, Process &p, uint64_t now, bool *fault);
	void finish_process(Process &p, uint64_t when);
	void end_segment(int core, uint64_t when);
	void collect_stats();
	uint64_t skip_idle_ticks(uint64_t now);

	Config cfg_;
	std::vector<Process> procs_;
	std::vector<Core> cores_;
	Cache l2_;
	VirtualMemory vm_;
	std::unique_ptr<Scheduler> sched_;
	ReadyQueue ready_;
	size_t done_ = 0;
	std::vector<GanttSegment> gantt_;
	std::vector<int> open_segment_;  // per core: index into gantt_, or -1
	SimStats stats_;
};
