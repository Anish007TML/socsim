// main.cpp - command-line front end.
//
//   ./socsim workloads/mixed.txt --cores 2 --sched rr --quantum 2000 --replace clock
//   ./socsim workloads/textbook_rr.txt --ideal --cores 1 --sched rr --quantum 4 --cs 0 --gantt
#include <cstdlib>
#include <iostream>
#include <string>

#include "simulator.h"

static void usage()
{
	std::cout <<
		"usage: socsim WORKLOAD [options]\n"
		"  --cores N          CPU cores (default 2)\n"
		"  --sched P          fcfs | sjf | rr | prio (default rr)\n"
		"  --quantum T        round-robin time slice in ticks (default 2000)\n"
		"  --cs T             context-switch cost in ticks (default 5)\n"
		"  --ideal            ideal memory: every access costs 1 tick (textbook mode)\n"
		"  --frames N         physical memory in 4 KiB frames (default 1024 = 4 MiB)\n"
		"  --replace P        fifo | lru | clock (default clock)\n"
		"  --tlb N            TLB entries per core (default 32)\n"
		"  --asid             tag TLB entries with the process id instead of flushing\n"
		"  --l1 KB  --l2 KB   cache sizes (default 32 and 256)\n"
		"  --fault T          page-fault service time in ticks (default 20000)\n"
		"  --seed S           random seed (default 1)\n"
		"  --gantt            print who ran on which core, when\n"
		"  --csv              print one CSV line instead of the report\n"
		"  --csv-header       print the CSV header and exit\n";
}

int main(int argc, char **argv)
{
	Config cfg;
	std::string workload;
	bool gantt = false, csv = false;

	try {
		for (int i = 1; i < argc; ++i) {
			std::string a = argv[i];
			auto next = [&]() -> std::string {
				if (i + 1 >= argc)
					throw std::invalid_argument("missing value after " + a);
				return argv[++i];
			};
			auto num = [&]() { return std::stoi(next()); };

			if (a == "-h" || a == "--help") { usage(); return 0; }
			else if (a == "--csv-header") { Simulator::print_csv_header(std::cout); return 0; }
			else if (a == "--cores") cfg.cores = num();
			else if (a == "--sched") cfg.sched = next();
			else if (a == "--quantum") cfg.quantum = num();
			else if (a == "--cs") cfg.cs_cost = num();
			else if (a == "--ideal") cfg.ideal_memory = true;
			else if (a == "--frames") cfg.frames = num();
			else if (a == "--replace") cfg.replace = next();
			else if (a == "--tlb") cfg.tlb_entries = num();
			else if (a == "--asid") cfg.asid = true;
			else if (a == "--l1") cfg.l1_kb = num();
			else if (a == "--l2") cfg.l2_kb = num();
			else if (a == "--fault") cfg.fault_ticks = num();
			else if (a == "--seed") cfg.seed = static_cast<uint32_t>(std::stoul(next()));
			else if (a == "--gantt") gantt = true;
			else if (a == "--csv") csv = true;
			else if (!a.empty() && a[0] == '-') throw std::invalid_argument("unknown option " + a);
			else workload = a;
		}
		if (workload.empty()) {
			usage();
			return 1;
		}
		Simulator sim(cfg, load_workload(workload, cfg.seed));
		sim.run();
		if (csv) {
			sim.print_csv(std::cout);
		} else {
			sim.print_report(std::cout);
			if (gantt) {
				std::cout << "\n";
				sim.print_gantt(std::cout);
			}
		}
	} catch (const std::exception &e) {
		std::cerr << "error: " << e.what() << "\n";
		return 1;
	}
	return 0;
}
