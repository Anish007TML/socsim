// scheduler.h - CPU scheduling policies.
//
// Each policy answers two questions:
//   pick()           which ready process should run next?
//   should_preempt() should the running process be taken off the CPU now?
#pragma once

#include <deque>
#include <memory>
#include <string>

#include "process.h"

using ReadyQueue = std::deque<Process *>;

class Scheduler {
public:
	virtual ~Scheduler() = default;
	virtual const char *name() const = 0;
	virtual size_t pick(const ReadyQueue &ready) const = 0;   // index into ready
	virtual bool should_preempt(const Process &running, const ReadyQueue &ready,
				    uint64_t quantum) const = 0;
};

// First-Come First-Served: run in arrival order, never preempt.
class FcfsScheduler : public Scheduler {
public:
	const char *name() const override { return "fcfs"; }
	size_t pick(const ReadyQueue &) const override { return 0; }
	bool should_preempt(const Process &, const ReadyQueue &, uint64_t) const override
	{
		return false;
	}
};

// Shortest Job First (non-preemptive): the shortest remaining work goes next.
class SjfScheduler : public Scheduler {
public:
	const char *name() const override { return "sjf"; }
	size_t pick(const ReadyQueue &ready) const override;
	bool should_preempt(const Process &, const ReadyQueue &, uint64_t) const override
	{
		return false;
	}
};

// Round Robin: everybody gets a time slice (quantum) in turn.
class RoundRobinScheduler : public Scheduler {
public:
	const char *name() const override { return "rr"; }
	size_t pick(const ReadyQueue &) const override { return 0; }
	bool should_preempt(const Process &running, const ReadyQueue &ready,
			    uint64_t quantum) const override
	{
		return running.slice_used >= quantum && !ready.empty();
	}
};

// Preemptive priority: a more important process takes the CPU immediately.
class PriorityScheduler : public Scheduler {
public:
	const char *name() const override { return "prio"; }
	size_t pick(const ReadyQueue &ready) const override;
	bool should_preempt(const Process &running, const ReadyQueue &ready,
			    uint64_t quantum) const override;
};

std::unique_ptr<Scheduler> make_scheduler(const std::string &name);
