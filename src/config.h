// config.h - every tunable of the simulated system, with defaults.
#pragma once

#include <cstdint>
#include <string>

struct Config {
	// CPU and scheduling
	int cores = 2;                 // number of CPU cores
	std::string sched = "rr";      // fcfs | sjf | rr | prio
	int quantum = 2000;            // round-robin time slice, in ticks
	int cs_cost = 5;               // ticks lost on every context switch

	// Memory hierarchy
	bool ideal_memory = false;     // true: every access costs 1 tick (textbook mode)
	int frames = 1024;             // physical memory in 4 KiB frames (1024 = 4 MiB)
	std::string replace = "clock"; // fifo | lru | clock
	int tlb_entries = 32;          // per-core TLB size
	bool asid = false;             // tag TLB entries with the process id (no flush on switch)
	int l1_kb = 32, l1_ways = 4;   // private L1 data cache per core
	int l2_kb = 256, l2_ways = 8;  // L2 cache shared by all cores

	// Latencies in ticks (1 tick ~ 1 CPU cycle). Quantum and page-fault time are
	// scaled down from real systems (milliseconds) so a run takes under a second.
	int lat_l1 = 1;
	int lat_l2 = 10;
	int lat_dram = 100;
	int lat_walk = 20;             // page-table walk after a TLB miss
	int fault_ticks = 20000;       // page fault: process sleeps while the page is "loaded from disk"

	uint32_t seed = 1;
};
