// cache.cpp - set-associative LRU cache.
#include "cache.h"

#include <stdexcept>

Cache::Cache(std::string name, uint32_t size_bytes, uint32_t ways, uint32_t line_bytes)
	: name_(std::move(name)), ways_(ways), line_bytes_(line_bytes)
{
	if (ways == 0 || line_bytes == 0 || size_bytes < ways * line_bytes)
		throw std::invalid_argument("bad cache geometry for " + name_);
	sets_ = size_bytes / (ways * line_bytes);
	lines_.resize(static_cast<size_t>(sets_) * ways_);
}

bool Cache::access(uint64_t paddr)
{
	uint64_t block = paddr / line_bytes_;        // which memory line
	uint32_t set = static_cast<uint32_t>(block % sets_);
	uint64_t tag = block / sets_;
	Line *row = &lines_[static_cast<size_t>(set) * ways_];
	Line *victim = &row[0];

	++clock_;
	for (uint32_t w = 0; w < ways_; ++w) {
		if (row[w].valid && row[w].tag == tag) {
			row[w].last_used = clock_;   // hit: now most recently used
			++hits_;
			return true;
		}
		// remember the best victim: an empty slot, else the oldest one
		if (!row[w].valid || (victim->valid && row[w].last_used < victim->last_used))
			victim = &row[w];
	}
	++misses_;
	victim->valid = true;
	victim->tag = tag;
	victim->last_used = clock_;
	return false;
}

void Cache::invalidate_all()
{
	for (Line &l : lines_)
		l.valid = false;
}

double Cache::hit_rate() const
{
	uint64_t total = hits_ + misses_;
	return total ? static_cast<double>(hits_) / static_cast<double>(total) : 0.0;
}
