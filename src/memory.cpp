// memory.cpp - page tables, frame allocation and replacement policies.
#include "memory.h"

#include <stdexcept>

// FIFO: evict the page that was loaded first, however often it is used.
size_t FifoPolicy::choose_victim(std::vector<Frame> &frames)
{
	size_t best = 0;

	for (size_t i = 1; i < frames.size(); ++i)
		if (frames[i].loaded_at < frames[best].loaded_at)
			best = i;
	return best;
}

// LRU: evict the page that has not been used for the longest time.
// Exact LRU needs a timestamp on EVERY memory access; real hardware does not
// provide that, which is why operating systems approximate it with Clock.
size_t LruPolicy::choose_victim(std::vector<Frame> &frames)
{
	size_t best = 0;

	for (size_t i = 1; i < frames.size(); ++i)
		if (frames[i].last_used < frames[best].last_used)
			best = i;
	return best;
}

// Clock (second chance): sweep the frames like a clock hand. A frame with its
// referenced bit set gets a second chance (bit cleared, hand moves on); the
// first frame found with the bit clear is evicted. Needs only one bit per
// page, which the MMU sets in hardware - this is what real kernels use.
size_t ClockPolicy::choose_victim(std::vector<Frame> &frames)
{
	for (;;) {
		Frame &f = frames[hand_];
		size_t here = hand_;

		hand_ = (hand_ + 1) % frames.size();
		if (!f.referenced)
			return here;
		f.referenced = false;
	}
}

std::unique_ptr<ReplacementPolicy> make_policy(const std::string &name)
{
	if (name == "fifo")
		return std::make_unique<FifoPolicy>();
	if (name == "lru")
		return std::make_unique<LruPolicy>();
	if (name == "clock")
		return std::make_unique<ClockPolicy>();
	throw std::invalid_argument("unknown replacement policy '" + name + "' (fifo|lru|clock)");
}

VirtualMemory::VirtualMemory(int num_frames, int num_processes,
			     std::unique_ptr<ReplacementPolicy> policy)
	: frames_(static_cast<size_t>(num_frames)),
	  page_tables_(static_cast<size_t>(num_processes), std::vector<int32_t>(VA_PAGES, -1)),
	  policy_(std::move(policy))
{
	if (num_frames <= 0)
		throw std::invalid_argument("need at least one physical frame");
}

VirtualMemory::Result VirtualMemory::translate(int pid, uint32_t vpn, uint64_t now)
{
	std::vector<int32_t> &pt = page_tables_.at(static_cast<size_t>(pid));
	Result r;
	size_t f;

	if (vpn >= VA_PAGES)
		throw std::out_of_range("virtual page outside the address space");
	if (pt[vpn] >= 0) {                      // page is present
		r.frame = static_cast<uint32_t>(pt[vpn]);
		return r;
	}

	// Page fault: find a free frame first ...
	r.fault = true;
	++faults_;
	for (f = 0; f < frames_.size() && frames_[f].used; ++f)
		;
	if (f == frames_.size()) {
		// ... none left: evict a victim and unmap it from its owner
		f = policy_->choose_victim(frames_);
		Frame &victim = frames_[f];
		page_tables_[static_cast<size_t>(victim.pid)][victim.vpn] = -1;
		r.evicted = true;
		r.victim_pid = victim.pid;
		r.victim_vpn = victim.vpn;
		++evictions_;
	}
	Frame &fr = frames_[f];
	fr.used = true;
	fr.pid = pid;
	fr.vpn = vpn;
	fr.referenced = true;
	fr.loaded_at = now;
	fr.last_used = now;
	pt[vpn] = static_cast<int32_t>(f);
	r.frame = static_cast<uint32_t>(f);
	return r;
}

void VirtualMemory::touch(uint32_t frame, uint64_t now)
{
	frames_[frame].referenced = true;
	frames_[frame].last_used = now;
}

void VirtualMemory::release_process(int pid)
{
	for (Frame &f : frames_)
		if (f.used && f.pid == pid)
			f = Frame{};
	for (int32_t &e : page_tables_.at(static_cast<size_t>(pid)))
		e = -1;
}

int VirtualMemory::frames_in_use() const
{
	int n = 0;

	for (const Frame &f : frames_)
		n += f.used;
	return n;
}
