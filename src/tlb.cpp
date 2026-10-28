// tlb.cpp - fully associative TLB with LRU replacement.
#include "tlb.h"

#include <stdexcept>

Tlb::Tlb(int entries)
{
	if (entries <= 0)
		throw std::invalid_argument("TLB needs at least one entry");
	entries_.resize(static_cast<size_t>(entries));
}

bool Tlb::lookup(int pid, uint32_t vpn, uint32_t *frame)
{
	++clock_;
	for (Entry &e : entries_) {
		if (e.valid && e.pid == pid && e.vpn == vpn) {
			e.last_used = clock_;
			*frame = e.frame;
			++hits_;
			return true;
		}
	}
	++misses_;
	return false;
}

void Tlb::insert(int pid, uint32_t vpn, uint32_t frame)
{
	Entry *victim = &entries_[0];

	for (Entry &e : entries_) {
		if (!e.valid) {            // free slot: use it
			victim = &e;
			break;
		}
		if (e.last_used < victim->last_used)
			victim = &e;           // otherwise the least recently used
	}
	victim->valid = true;
	victim->pid = pid;
	victim->vpn = vpn;
	victim->frame = frame;
	victim->last_used = ++clock_;
}

void Tlb::flush()
{
	for (Entry &e : entries_)
		e.valid = false;
	++flushes_;
}

void Tlb::invalidate(int pid, uint32_t vpn)
{
	for (Entry &e : entries_)
		if (e.valid && e.pid == pid && e.vpn == vpn)
			e.valid = false;
}

void Tlb::invalidate_process(int pid)
{
	for (Entry &e : entries_)
		if (e.valid && e.pid == pid)
			e.valid = false;
}
