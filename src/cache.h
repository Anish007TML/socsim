// cache.h - a set-associative CPU cache with LRU replacement.
//
// An address is split into   [ tag | set index | offset in line ]
// The set index picks one row of the cache; the line may live in any of the
// 'ways' slots of that row. On a miss the least recently used slot is replaced.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Cache {
public:
	Cache(std::string name, uint32_t size_bytes, uint32_t ways, uint32_t line_bytes = 64);

	// Returns true on a hit. On a miss the line is brought in (evicting LRU).
	bool access(uint64_t paddr);
	void invalidate_all();

	const std::string &name() const { return name_; }
	uint64_t hits() const { return hits_; }
	uint64_t misses() const { return misses_; }
	double hit_rate() const;
	uint32_t num_sets() const { return sets_; }

private:
	struct Line {
		bool valid = false;
		uint64_t tag = 0;
		uint64_t last_used = 0;   // for LRU
	};

	std::string name_;
	uint32_t sets_, ways_, line_bytes_;
	std::vector<Line> lines_;     // sets_ * ways_ entries, row by row
	uint64_t clock_ = 0;          // increases on every access
	uint64_t hits_ = 0, misses_ = 0;
};
