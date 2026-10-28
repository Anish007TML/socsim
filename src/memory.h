// memory.h - virtual memory: per-process page tables, physical frames and
// page-replacement policies.
//
//   virtual address (32 bit) = [ virtual page number (20 bit) | offset (12 bit) ]
//   page table[pid][vpn]     -> physical frame number, or "not present"
//
// When a page is not present (a page fault) the OS takes a free frame, or
// evicts a page chosen by the replacement policy (FIFO, LRU or Clock).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

constexpr uint32_t PAGE_SHIFT = 12;                 // 4 KiB pages
constexpr uint32_t PAGE_SIZE = 1u << PAGE_SHIFT;
constexpr uint32_t VA_PAGES = 4096;                 // 16 MiB virtual space per process

struct Frame {
	bool used = false;
	int pid = -1;            // owner
	uint32_t vpn = 0;        // which virtual page lives here
	bool referenced = false; // "accessed" bit, set by hardware on every use (Clock)
	uint64_t loaded_at = 0;  // for FIFO
	uint64_t last_used = 0;  // for LRU
};

// Strategy pattern: each policy decides which used frame to evict.
class ReplacementPolicy {
public:
	virtual ~ReplacementPolicy() = default;
	virtual const char *name() const = 0;
	virtual size_t choose_victim(std::vector<Frame> &frames) = 0;
};

class FifoPolicy : public ReplacementPolicy {
public:
	const char *name() const override { return "fifo"; }
	size_t choose_victim(std::vector<Frame> &frames) override;
};

class LruPolicy : public ReplacementPolicy {
public:
	const char *name() const override { return "lru"; }
	size_t choose_victim(std::vector<Frame> &frames) override;
};

class ClockPolicy : public ReplacementPolicy {
public:
	const char *name() const override { return "clock"; }
	size_t choose_victim(std::vector<Frame> &frames) override;

private:
	size_t hand_ = 0;        // the clock hand sweeps over the frames
};

std::unique_ptr<ReplacementPolicy> make_policy(const std::string &name);

class VirtualMemory {
public:
	struct Result {
		uint32_t frame = 0;
		bool fault = false;      // page was not present
		bool evicted = false;    // another page had to be thrown out
		int victim_pid = -1;
		uint32_t victim_vpn = 0;
	};

	VirtualMemory(int num_frames, int num_processes, std::unique_ptr<ReplacementPolicy> policy);

	// Look up (and on a fault, load) the page. Called on a TLB miss.
	Result translate(int pid, uint32_t vpn, uint64_t now);
	// Record a use of a frame (sets the referenced bit / LRU time).
	void touch(uint32_t frame, uint64_t now);
	// Process exited: give all its frames back.
	void release_process(int pid);

	uint64_t faults() const { return faults_; }
	uint64_t evictions() const { return evictions_; }
	int frames_in_use() const;
	const char *policy_name() const { return policy_->name(); }

private:
	std::vector<Frame> frames_;
	std::vector<std::vector<int32_t>> page_tables_;   // [pid][vpn] -> frame or -1
	std::unique_ptr<ReplacementPolicy> policy_;
	uint64_t faults_ = 0, evictions_ = 0;
};
