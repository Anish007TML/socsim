// scheduler.cpp - scheduling policies. Ties are broken by queue order (FIFO).
#include "scheduler.h"

#include <stdexcept>

size_t SjfScheduler::pick(const ReadyQueue &ready) const
{
	size_t best = 0;

	for (size_t i = 1; i < ready.size(); ++i)
		if (ready[i]->remaining < ready[best]->remaining)
			best = i;
	return best;
}

size_t PriorityScheduler::pick(const ReadyQueue &ready) const
{
	size_t best = 0;

	for (size_t i = 1; i < ready.size(); ++i)
		if (ready[i]->priority < ready[best]->priority)
			best = i;
	return best;
}

bool PriorityScheduler::should_preempt(const Process &running, const ReadyQueue &ready,
				       uint64_t) const
{
	for (const Process *p : ready)
		if (p->priority < running.priority)
			return true;
	return false;
}

std::unique_ptr<Scheduler> make_scheduler(const std::string &name)
{
	if (name == "fcfs")
		return std::make_unique<FcfsScheduler>();
	if (name == "sjf")
		return std::make_unique<SjfScheduler>();
	if (name == "rr")
		return std::make_unique<RoundRobinScheduler>();
	if (name == "prio")
		return std::make_unique<PriorityScheduler>();
	throw std::invalid_argument("unknown scheduler '" + name + "' (fcfs|sjf|rr|prio)");
}
