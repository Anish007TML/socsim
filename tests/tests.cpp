// tests.cpp - unit tests. The scheduling and page-replacement results are
// checked against the worked examples in Silberschatz, "Operating System
// Concepts" (chapters 5 and 10), so the simulator agrees with the textbook.
#include <cmath>
#include <cstdio>
#include <vector>

#include "cache.h"
#include "memory.h"
#include "simulator.h"
#include "tlb.h"

static int g_checks = 0, g_failures = 0;

#define CHECK(cond)                                                                  \
	do {                                                                         \
		++g_checks;                                                          \
		if (!(cond)) {                                                       \
			++g_failures;                                                \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                    \
	} while (0)

#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 0.01)

#define RUN(test)                                                                     \
	do {                                                                          \
		int before = g_failures;                                              \
		test();                                                               \
		std::printf("%s %s\n", g_failures == before ? "[ OK ]" : "[FAIL]", #test); \
	} while (0)

// ---------------------------------------------------------------- cache ----

static void test_cache_hits_and_lru()
{
	Cache c("test", 1024, 2, 64);       // 1 KiB, 2-way, 64 B lines -> 8 sets
	CHECK(c.num_sets() == 8);
	CHECK(!c.access(0));                 // cold miss
	CHECK(c.access(0));                  // hit
	CHECK(c.access(60));                 // same 64-byte line: hit (spatial locality)
	CHECK(!c.access(64));                // next line, different set: miss

	// 0, 512 and 1024 all map to set 0 (8 sets x 64 B = 512 B apart)
	CHECK(!c.access(512));               // set 0 now holds {0, 512}
	CHECK(c.access(0));                  // 0 becomes most recently used
	CHECK(!c.access(1024));              // evicts 512 (the LRU one), not 0
	CHECK(c.access(0));
	CHECK(!c.access(512));               // 512 was evicted
	CHECK(c.hits() == 4 && c.misses() == 5);
	c.invalidate_all();
	CHECK(!c.access(0));
}

// ------------------------------------------------------------------ TLB ----

static void test_tlb()
{
	Tlb t(4);
	uint32_t f = 0;

	CHECK(!t.lookup(1, 10, &f));
	t.insert(1, 10, 100);
	CHECK(t.lookup(1, 10, &f) && f == 100);
	CHECK(!t.lookup(2, 10, &f));         // same page number, other process: miss (ASID tag)
	for (uint32_t v = 11; v <= 13; ++v)
		t.insert(1, v, v * 10);
	CHECK(t.lookup(1, 10, &f));          // touch 10 so 11 becomes LRU
	t.insert(1, 14, 140);                // full: evicts vpn 11
	CHECK(!t.lookup(1, 11, &f));
	CHECK(t.lookup(1, 14, &f) && f == 140);
	t.invalidate(1, 14);                 // shootdown
	CHECK(!t.lookup(1, 14, &f));
	t.flush();
	CHECK(!t.lookup(1, 10, &f));
	CHECK(t.flushes() == 1);
}

// --------------------------------------------------- page replacement ----

static int count_faults(const std::vector<uint32_t> &refs, int frames, const char *policy)
{
	VirtualMemory vm(frames, 1, make_policy(policy));
	uint64_t t = 0;

	for (uint32_t vpn : refs) {
		VirtualMemory::Result r = vm.translate(0, vpn, ++t);
		vm.touch(r.frame, t);
	}
	return static_cast<int>(vm.faults());
}

static void test_page_replacement_textbook()
{
	// Silberschatz reference string, 3 frames: FIFO 15, LRU 12 faults.
	const std::vector<uint32_t> refs = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
	CHECK(count_faults(refs, 3, "fifo") == 15);
	CHECK(count_faults(refs, 3, "lru") == 12);
	CHECK(count_faults(refs, 3, "clock") == 14);   // traced by hand in docs/HOW_IT_WORKS.md
}

static void test_belady_anomaly()
{
	// With FIFO, MORE memory can cause MORE page faults (Belady's anomaly).
	const std::vector<uint32_t> refs = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
	CHECK(count_faults(refs, 3, "fifo") == 9);
	CHECK(count_faults(refs, 4, "fifo") == 10);
	// LRU is a "stack algorithm": it never gets worse with more frames.
	CHECK(count_faults(refs, 4, "lru") <= count_faults(refs, 3, "lru"));
}

static void test_page_tables_are_per_process()
{
	VirtualMemory vm(2, 2, make_policy("lru"));
	VirtualMemory::Result a = vm.translate(0, 5, 1);    // pid 0, page 5
	VirtualMemory::Result b = vm.translate(1, 5, 2);    // pid 1, SAME page number
	CHECK(a.fault && b.fault && a.frame != b.frame);    // different physical frames
	VirtualMemory::Result again = vm.translate(0, 5, 3);
	CHECK(!again.fault);                                // now present
	vm.touch(again.frame, 3);                           // pid 0 used its page recently
	VirtualMemory::Result c = vm.translate(1, 6, 4);    // memory full: evict LRU = pid 1's
	CHECK(c.evicted && c.victim_pid == 1 && c.victim_vpn == 5);
	CHECK(vm.translate(1, 5, 5).fault);                 // was evicted
	vm.release_process(0);
	vm.release_process(1);
	CHECK(vm.frames_in_use() == 0);
}

// ----------------------------------------------------------- scheduling ----

static Simulator run_textbook(const char *workload, const char *sched, int quantum = 4, int cores = 1)
{
	Config cfg;
	cfg.ideal_memory = true;
	cfg.cores = cores;
	cfg.cs_cost = 0;
	cfg.sched = sched;
	cfg.quantum = quantum;
	Simulator sim(cfg, parse_workload(workload, 1));
	sim.run();
	return sim;
}

static const char *kFcfs = "P1 0 24 0 - 0\nP2 0 3 0 - 0\nP3 0 3 0 - 0\n";

static void test_fcfs_textbook()
{
	Simulator s = run_textbook(kFcfs, "fcfs");
	CHECK(s.processes()[0].wait == 0);
	CHECK(s.processes()[1].wait == 24);
	CHECK(s.processes()[2].wait == 27);
	CHECK_NEAR(s.stats().avg_wait, 17.0);
	CHECK(s.stats().makespan == 30);
}

static void test_rr_textbook()
{
	Simulator s = run_textbook(kFcfs, "rr", 4);
	CHECK(s.processes()[0].wait == 6);
	CHECK(s.processes()[1].wait == 4);
	CHECK(s.processes()[2].wait == 7);
	CHECK_NEAR(s.stats().avg_wait, 17.0 / 3.0);         // 5.67
	// Gantt chart: P1 0-4, P2 4-7, P3 7-10, P1 10-30
	const auto &g = s.gantt();
	CHECK(g.size() == 4);
	CHECK(g[0].pid == 0 && g[0].end == 4 && g[1].pid == 1 && g[1].end == 7);
	CHECK(g[2].pid == 2 && g[2].end == 10 && g[3].pid == 0 && g[3].end == 30);
}

static void test_sjf_textbook()
{
	Simulator s = run_textbook("P1 0 6 0 - 0\nP2 0 8 0 - 0\nP3 0 7 0 - 0\nP4 0 3 0 - 0\n", "sjf");
	CHECK(s.processes()[3].wait == 0);                   // shortest first
	CHECK_NEAR(s.stats().avg_wait, 7.0);
}

static void test_priority_textbook()
{
	Simulator s = run_textbook(
		"P1 0 10 3 - 0\nP2 0 1 1 - 0\nP3 0 2 4 - 0\nP4 0 1 5 - 0\nP5 0 5 2 - 0\n", "prio");
	CHECK_NEAR(s.stats().avg_wait, 8.2);
}

static void test_priority_preemption()
{
	// A high-priority job arriving later takes the CPU immediately.
	Simulator s = run_textbook("low 0 10 5 - 0\nhigh 3 2 1 - 0\n", "prio");
	CHECK(s.processes()[1].first_run == 3 && s.processes()[1].finish == 5);
	CHECK(s.processes()[0].finish == 12);
	CHECK(s.stats().preemptions == 1);
}

static void test_two_cores()
{
	Simulator s = run_textbook(kFcfs, "fcfs", 4, 2);     // P1 on core 0; P2 then P3 on core 1
	CHECK(s.stats().makespan == 24);
	CHECK_NEAR(s.stats().avg_wait, 1.0);                 // only P3 waits (3 ticks)
}

// ------------------------------------------------ full memory hierarchy ----

static const char *kMem =
	"a 0 40000 1 hotcold 256\n"
	"b 0 40000 1 random 128\n"
	"c 0 40000 1 seq 256\n";

static SimStats run_mem(bool asid, int frames, int quantum = 500)
{
	Config cfg;
	cfg.cores = 1;
	cfg.asid = asid;
	cfg.frames = frames;
	cfg.quantum = quantum;
	Simulator sim(cfg, parse_workload(kMem, 7));
	sim.run();
	return sim.stats();
}

static void test_asid_avoids_tlb_flushes()
{
	SimStats flush = run_mem(false, 1024), tagged = run_mem(true, 1024);
	CHECK(flush.tlb_flushes > 0 && tagged.tlb_flushes == 0);
	CHECK(tagged.tlb_hit_rate() > flush.tlb_hit_rate());
	CHECK(tagged.makespan < flush.makespan);
}

static void test_memory_pressure()
{
	SimStats plenty = run_mem(true, 1024), tight = run_mem(true, 64);
	// Only compulsory faults (first touch of each page): a and b touch all their
	// pages (64 + 32); c reads 40000 words = 160 KB sequentially = 40 pages.
	CHECK(plenty.page_faults == 64 + 32 + 40);
	CHECK(plenty.evictions == 0);
	CHECK(tight.page_faults > plenty.page_faults);       // thrashing
	CHECK(tight.evictions > 0 && tight.tlb_shootdowns == tight.evictions);
	CHECK(tight.makespan > plenty.makespan);
}

static void test_deterministic()
{
	SimStats a = run_mem(false, 100), b = run_mem(false, 100);
	CHECK(a.makespan == b.makespan && a.page_faults == b.page_faults);
}

static void test_bad_input_rejected()
{
	bool threw = false;
	try {
		parse_workload("x 0 10 0 bogus 4\n", 1);
	} catch (const std::exception &) {
		threw = true;
	}
	CHECK(threw);
	threw = false;
	try {
		Config cfg;                                       // memory model on, but no pattern
		Simulator sim(cfg, parse_workload("x 0 10 0 - 0\n", 1));
	} catch (const std::exception &) {
		threw = true;
	}
	CHECK(threw);
}

int main()
{
	RUN(test_cache_hits_and_lru);
	RUN(test_tlb);
	RUN(test_page_replacement_textbook);
	RUN(test_belady_anomaly);
	RUN(test_page_tables_are_per_process);
	RUN(test_fcfs_textbook);
	RUN(test_rr_textbook);
	RUN(test_sjf_textbook);
	RUN(test_priority_textbook);
	RUN(test_priority_preemption);
	RUN(test_two_cores);
	RUN(test_asid_avoids_tlb_flushes);
	RUN(test_memory_pressure);
	RUN(test_deterministic);
	RUN(test_bad_input_rejected);
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
