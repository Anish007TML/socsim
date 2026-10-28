// tlb.h - Translation Lookaside Buffer: a small cache of page-table entries.
//
// Every process uses the same virtual addresses, so a TLB entry must know
// which process it belongs to. Two classic solutions:
//   * flush the whole TLB on every context switch (simple, but the next
//     process starts with an empty TLB), or
//   * tag each entry with an Address Space ID (ASID) = process id, so entries
//     of different processes can live side by side (ARM and x86 PCID do this).
#pragma once

#include <cstdint>
#include <vector>

class Tlb {
public:
	explicit Tlb(int entries);

	bool lookup(int pid, uint32_t vpn, uint32_t *frame);   // true on hit
	void insert(int pid, uint32_t vpn, uint32_t frame);
	void flush();                                          // drop everything
	void invalidate(int pid, uint32_t vpn);                // drop one mapping ("shootdown")
	void invalidate_process(int pid);

	uint64_t hits() const { return hits_; }
	uint64_t misses() const { return misses_; }
	uint64_t flushes() const { return flushes_; }

private:
	struct Entry {
		bool valid = false;
		int pid = -1;
		uint32_t vpn = 0;
		uint32_t frame = 0;
		uint64_t last_used = 0;
	};
	std::vector<Entry> entries_;   // fully associative: any entry can hold any page
	uint64_t clock_ = 0;
	uint64_t hits_ = 0, misses_ = 0, flushes_ = 0;
};
