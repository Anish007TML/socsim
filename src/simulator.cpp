// simulator.cpp - the main simulation loop.
#include "simulator.h"

#include <algorithm>
#include <iomanip>
#include <stdexcept>

Simulator::Core::Core(const Config &c)
	: tlb(c.tlb_entries),
	  l1("L1", static_cast<uint32_t>(c.l1_kb) * 1024, static_cast<uint32_t>(c.l1_ways))
{
}

Simulator::Simulator(const Config &cfg, std::vector<Process> procs)
	: cfg_(cfg), procs_(std::move(procs)),
	  l2_("L2", static_cast<uint32_t>(cfg.l2_kb) * 1024, static_cast<uint32_t>(cfg.l2_ways)),
	  vm_(cfg.frames, static_cast<int>(procs_.size()), make_policy(cfg.replace)),
	  sched_(make_scheduler(cfg.sched))
{
	if (cfg.cores < 1 || cfg.cores > 64)
		throw std::invalid_argument("cores must be 1..64");
	if (cfg.quantum < 1 || cfg.cs_cost < 0)
		throw std::invalid_argument("quantum must be >= 1 and cs_cost >= 0");
	for (const Process &p : procs_)
		if (!cfg.ideal_memory && p.pattern == Pattern::None)
			throw std::invalid_argument("process '" + p.name +
						    "' has no memory pattern: use --ideal");
	for (int i = 0; i < cfg.cores; ++i)
		cores_.emplace_back(cfg);
	open_segment_.assign(static_cast<size_t>(cfg.cores), -1);
}

// New arrivals and processes whose page fault has been serviced join the
// tail of the ready queue.
void Simulator::admit_and_wake(uint64_t now)
{
	for (Process &p : procs_) {
		if (p.state == State::NotArrived && p.arrival <= now) {
			p.state = State::Ready;
			ready_.push_back(&p);
		} else if (p.state == State::Blocked && p.wake_at <= now) {
			if (p.remaining == 0) {
				finish_process(p, now);
			} else {
				p.state = State::Ready;
				ready_.push_back(&p);
			}
		}
	}
}

void Simulator::end_segment(int core, uint64_t when)
{
	int &seg = open_segment_[static_cast<size_t>(core)];

	if (seg >= 0) {
		gantt_[static_cast<size_t>(seg)].end = when;
		seg = -1;
	}
}

void Simulator::finish_process(Process &p, uint64_t when)
{
	p.state = State::Done;
	p.finish = static_cast<int64_t>(when);
	vm_.release_process(p.pid);              // free its memory ...
	for (Core &c : cores_)
		c.tlb.invalidate_process(p.pid);     // ... and its stale translations
	++done_;
}

// One memory access: TLB -> (page walk -> page fault?) -> L1 -> L2 -> DRAM.
// Returns how many ticks it costs. Sets *fault if the page was not present.
uint64_t Simulator::memory_access(Core &core, Process &p, uint64_t now, bool *fault)
{
	uint32_t va = p.next_address();
	uint32_t vpn = va >> PAGE_SHIFT, frame = 0;
	uint64_t ticks = 0;

	*fault = false;
	if (!core.tlb.lookup(p.pid, vpn, &frame)) {
		ticks += static_cast<uint64_t>(cfg_.lat_walk);        // walk the page table
		VirtualMemory::Result r = vm_.translate(p.pid, vpn, now);

		if (r.evicted) {
			// The victim page may be cached in ANY core's TLB: shoot it down.
			for (Core &c : cores_)
				c.tlb.invalidate(r.victim_pid, r.victim_vpn);
			++stats_.tlb_shootdowns;
		}
		core.tlb.insert(p.pid, vpn, r.frame);
		if (r.fault) {
			*fault = true;
			++p.faults;
			return 0;
		}
		frame = r.frame;
	}
	vm_.touch(frame, now);

	uint64_t paddr = (static_cast<uint64_t>(frame) << PAGE_SHIFT) | (va & (PAGE_SIZE - 1));
	if (core.l1.access(paddr))
		ticks += static_cast<uint64_t>(cfg_.lat_l1);
	else if (l2_.access(paddr))
		ticks += static_cast<uint64_t>(cfg_.lat_l2);
	else
		ticks += static_cast<uint64_t>(cfg_.lat_dram);
	++stats_.accesses;
	stats_.memory_ticks += ticks;
	return ticks;
}

void Simulator::step_core(int id, uint64_t now)
{
	Core &core = cores_[static_cast<size_t>(id)];
	Process *p = core.running;

	if (core.busy_until > now)
		return;                                // still busy with the last access

	// 1. Should the running process give up the CPU?
	if (p && sched_->should_preempt(*p, ready_, static_cast<uint64_t>(cfg_.quantum))) {
		p->state = State::Ready;
		ready_.push_back(p);
		core.running = p = nullptr;
		end_segment(id, now);
		++stats_.preemptions;
	}

	// 2. Idle core: dispatch the process the policy picks.
	if (!p) {
		if (ready_.empty())
			return;
		size_t i = sched_->pick(ready_);
		p = ready_[i];
		ready_.erase(ready_.begin() + static_cast<long>(i));
		p->state = State::Running;
		p->slice_used = 0;
		if (p->first_run < 0)
			p->first_run = static_cast<int64_t>(now);
		core.running = p;
		gantt_.push_back({id, p->pid, now, now});
		open_segment_[static_cast<size_t>(id)] = static_cast<int>(gantt_.size() - 1);

		if (core.last_pid != p->pid) {         // a real context switch
			++stats_.context_switches;
			core.last_pid = p->pid;
			if (!cfg_.asid)
				core.tlb.flush();              // old process's translations are invalid
			if (cfg_.cs_cost > 0) {
				core.busy_until = now + static_cast<uint64_t>(cfg_.cs_cost);
				return;                        // save/restore registers etc.
			}
		}
	}

	// 3. Execute one unit of work (one memory access).
	bool fault = false;
	uint64_t ticks = cfg_.ideal_memory ? 1 : memory_access(core, *p, now, &fault);

	--p->remaining;
	if (fault) {
		// The process sleeps while the page is read from disk; the core is free
		// to run somebody else - this is why multiprogramming helps.
		p->state = State::Blocked;
		p->wake_at = now + static_cast<uint64_t>(cfg_.fault_ticks);
		core.running = nullptr;
		core.busy_until = now + 1;
		end_segment(id, now + 1);
		return;
	}
	p->slice_used += ticks;
	core.busy_until = now + ticks;
	if (p->remaining == 0) {
		finish_process(*p, now + ticks);
		core.running = nullptr;
		end_segment(id, now + ticks);
	}
}

void Simulator::run()
{
	const uint64_t limit = 1'000'000'000'000ull;   // safety net against bugs
	uint64_t now = 0;

	for (; done_ < procs_.size(); ++now) {
		if (now > limit)
			throw std::runtime_error("simulation did not finish (tick limit)");
		admit_and_wake(now);
		for (int c = 0; c < cfg_.cores; ++c)
			step_core(c, now);
		for (Process *p : ready_)
			++p->wait;                          // waiting time = time spent ready
		for (Core &c : cores_)
			if (c.running || c.busy_until > now)
				++c.busy_ticks;
		now = skip_idle_ticks(now);
	}
	collect_stats();
}

// Speed-up only, does not change any result: if nothing can happen in the
// next ticks (all cores busy or idle, nobody waking up or arriving), jump
// straight to the next event and account for the skipped ticks in bulk.
uint64_t Simulator::skip_idle_ticks(uint64_t now)
{
	uint64_t next = UINT64_MAX;

	for (const Core &c : cores_) {
		if (c.busy_until > now + 1)
			next = std::min(next, c.busy_until);   // finishes its access then
		else if (c.running || !ready_.empty())
			return now;                            // can act on the very next tick
	}
	for (const Process &p : procs_) {
		if (p.state == State::Blocked)
			next = std::min(next, p.wake_at);
		else if (p.state == State::NotArrived)
			next = std::min(next, p.arrival);
	}
	if (next == UINT64_MAX || next <= now + 1)
		return now;

	uint64_t skipped = next - (now + 1);
	for (Process *p : ready_)
		p->wait += skipped;
	for (Core &c : cores_)
		if (c.busy_until > now + 1)
			c.busy_ticks += skipped;
	return next - 1;                               // the loop's ++now lands on 'next'
}

void Simulator::collect_stats()
{
	double n = static_cast<double>(procs_.size());

	for (const Process &p : procs_) {
		stats_.makespan = std::max(stats_.makespan, static_cast<uint64_t>(p.finish));
		stats_.avg_wait += static_cast<double>(p.wait) / n;
		stats_.avg_turnaround += static_cast<double>(p.turnaround()) / n;
		stats_.avg_response += static_cast<double>(p.response()) / n;
	}
	for (const Core &c : cores_) {
		stats_.tlb_hits += c.tlb.hits();
		stats_.tlb_misses += c.tlb.misses();
		stats_.tlb_flushes += c.tlb.flushes();
		stats_.l1_hits += c.l1.hits();
		stats_.l1_misses += c.l1.misses();
		stats_.utilization.push_back(
			stats_.makespan ? std::min(1.0, static_cast<double>(c.busy_ticks) /
								static_cast<double>(stats_.makespan))
					: 0.0);
	}
	stats_.l2_hits = l2_.hits();
	stats_.l2_misses = l2_.misses();
	stats_.page_faults = vm_.faults();
	stats_.evictions = vm_.evictions();
}

static double ratio(uint64_t a, uint64_t b)
{
	return a + b ? static_cast<double>(a) / static_cast<double>(a + b) : 0.0;
}

double SimStats::tlb_hit_rate() const { return ratio(tlb_hits, tlb_misses); }
double SimStats::l1_hit_rate() const { return ratio(l1_hits, l1_misses); }
double SimStats::l2_hit_rate() const { return ratio(l2_hits, l2_misses); }
double SimStats::amat() const
{
	return accesses ? static_cast<double>(memory_ticks) / static_cast<double>(accesses) : 0.0;
}

void Simulator::print_report(std::ostream &out) const
{
	const SimStats &s = stats_;

	out << std::fixed << std::setprecision(1);
	out << "System: " << cfg_.cores << " cores, scheduler " << sched_->name();
	if (cfg_.sched == "rr")
		out << " (quantum " << cfg_.quantum << ")";
	out << ", context switch " << cfg_.cs_cost << " ticks\n";
	if (cfg_.ideal_memory)
		out << "Memory: ideal (every access = 1 tick)\n";
	else
		out << "Memory: " << cfg_.frames << " frames x 4 KiB, replacement " << vm_.policy_name()
		    << ", TLB " << cfg_.tlb_entries << " entries/core (" << (cfg_.asid ? "ASID tags" : "flush on switch")
		    << "), L1 " << cfg_.l1_kb << " KiB/core, L2 " << cfg_.l2_kb << " KiB shared\n";

	out << "\n" << std::left << std::setw(12) << "process" << std::right << std::setw(8) << "arrive"
	    << std::setw(10) << "burst" << std::setw(6) << "prio" << std::setw(10) << "finish"
	    << std::setw(12) << "turnaround" << std::setw(10) << "waiting" << std::setw(10) << "response"
	    << std::setw(8) << "faults" << "\n";
	for (const Process &p : procs_)
		out << std::left << std::setw(12) << p.name << std::right << std::setw(8) << p.arrival
		    << std::setw(10) << p.burst << std::setw(6) << p.priority << std::setw(10) << p.finish
		    << std::setw(12) << p.turnaround() << std::setw(10) << p.wait << std::setw(10)
		    << p.response() << std::setw(8) << p.faults << "\n";
	out << std::left << std::setw(46) << "average" << std::right << std::setw(12) << s.avg_turnaround
	    << std::setw(10) << s.avg_wait << std::setw(10) << s.avg_response << "\n\n";

	out << "makespan " << s.makespan << " ticks, " << s.context_switches << " context switches ("
	    << s.preemptions << " preemptions), CPU utilization:";
	for (double u : s.utilization)
		out << " " << u * 100 << "%";
	out << "\n";
	if (!cfg_.ideal_memory) {
		out << std::setprecision(2);
		out << "TLB hit rate " << s.tlb_hit_rate() * 100 << "% (" << s.tlb_flushes << " flushes, "
		    << s.tlb_shootdowns << " shootdowns)   L1 hit rate " << s.l1_hit_rate() * 100
		    << "%   L2 hit rate " << s.l2_hit_rate() * 100 << "%\n";
		out << "page faults " << s.page_faults << " (" << s.evictions << " evictions)   "
		    << "average memory access time " << s.amat() << " ticks\n";
	}
}

void Simulator::print_gantt(std::ostream &out, size_t max_segments) const
{
	for (int c = 0; c < cfg_.cores; ++c) {
		size_t shown = 0;

		out << "core " << c << ": |";
		for (const GanttSegment &g : gantt_) {
			if (g.core != c)
				continue;
			if (++shown > max_segments) {
				out << " ...";
				break;
			}
			out << " " << procs_[static_cast<size_t>(g.pid)].name << " " << g.start << "-" << g.end
			    << " |";
		}
		out << "\n";
	}
}

void Simulator::print_csv_header(std::ostream &out)
{
	out << "cores,sched,quantum,replace,frames,asid,makespan,avg_wait,avg_turnaround,avg_response,"
	       "context_switches,tlb_hit_rate,l1_hit_rate,l2_hit_rate,page_faults,amat,utilization\n";
}

void Simulator::print_csv(std::ostream &out) const
{
	const SimStats &s = stats_;
	double util = 0;

	for (double u : s.utilization)
		util += u / static_cast<double>(s.utilization.size());
	out << std::fixed << std::setprecision(4) << cfg_.cores << "," << cfg_.sched << ","
	    << cfg_.quantum << "," << cfg_.replace << "," << cfg_.frames << "," << (cfg_.asid ? 1 : 0)
	    << "," << s.makespan << "," << s.avg_wait << "," << s.avg_turnaround << ","
	    << s.avg_response << "," << s.context_switches << "," << s.tlb_hit_rate() << ","
	    << s.l1_hit_rate() << "," << s.l2_hit_rate() << "," << s.page_faults << "," << s.amat()
	    << "," << util << "\n";
}
