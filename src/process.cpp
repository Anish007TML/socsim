// process.cpp - workload parsing and address generation.
#include "process.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "memory.h"

// xorshift32: a tiny, fast pseudo-random generator (deterministic runs).
static uint32_t xorshift(uint32_t &s)
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

uint32_t Process::next_address()
{
	const uint32_t line = 64;

	switch (pattern) {
	case Pattern::Seq:
		cursor = (cursor + 4) % wss_bytes;            // next 32-bit word
		return cursor;
	case Pattern::Random:
		return (xorshift(rng) % wss_bytes) & ~3u;
	case Pattern::HotCold: {
		uint32_t hot = wss_bytes / 10 ? wss_bytes / 10 : wss_bytes;

		if (xorshift(rng) % 100 < 90)
			return (xorshift(rng) % hot) & ~3u;       // hot 10% of the data
		return (xorshift(rng) % wss_bytes) & ~3u;     // anywhere
	}
	case Pattern::Stride:
		cursor = (cursor + PAGE_SIZE + line) % wss_bytes;
		return cursor;
	case Pattern::None:
		break;
	}
	return 0;
}

Pattern parse_pattern(const std::string &s)
{
	if (s == "-" || s == "none")
		return Pattern::None;
	if (s == "seq")
		return Pattern::Seq;
	if (s == "random")
		return Pattern::Random;
	if (s == "hotcold")
		return Pattern::HotCold;
	if (s == "stride")
		return Pattern::Stride;
	throw std::invalid_argument("unknown pattern '" + s + "' (seq|random|hotcold|stride|-)");
}

const char *pattern_name(Pattern p)
{
	switch (p) {
	case Pattern::Seq: return "seq";
	case Pattern::Random: return "random";
	case Pattern::HotCold: return "hotcold";
	case Pattern::Stride: return "stride";
	case Pattern::None: break;
	}
	return "-";
}

std::vector<Process> parse_workload(const std::string &text, uint32_t seed)
{
	std::vector<Process> procs;
	std::istringstream in(text);
	std::string line;
	int lineno = 0;

	while (std::getline(in, line)) {
		++lineno;
		line = line.substr(0, line.find('#'));
		std::istringstream fields(line);
		Process p;
		std::string pattern;
		uint64_t wss_kb = 0;

		if (!(fields >> p.name))
			continue; // blank or comment line
		if (!(fields >> p.arrival >> p.burst >> p.priority >> pattern >> wss_kb))
			throw std::invalid_argument("line " + std::to_string(lineno) +
						    ": expected 'name arrival burst priority pattern wss_kb'");
		p.pattern = parse_pattern(pattern);
		if (p.burst == 0)
			throw std::invalid_argument("line " + std::to_string(lineno) + ": burst must be > 0");
		if (p.pattern != Pattern::None && (wss_kb == 0 || wss_kb * 1024 > VA_PAGES * PAGE_SIZE))
			throw std::invalid_argument("line " + std::to_string(lineno) +
						    ": wss_kb must be 1..16384");
		p.wss_bytes = static_cast<uint32_t>(wss_kb * 1024);
		p.pid = static_cast<int>(procs.size());
		p.remaining = p.burst;
		p.rng = (seed * 2654435761u) ^ (static_cast<uint32_t>(p.pid) * 40503u + 1u);
		if (p.rng == 0)
			p.rng = 1;
		procs.push_back(p);
	}
	if (procs.empty())
		throw std::invalid_argument("workload has no processes");
	return procs;
}

std::vector<Process> load_workload(const std::string &path, uint32_t seed)
{
	std::ifstream f(path);
	std::stringstream ss;

	if (!f)
		throw std::runtime_error("cannot open workload file " + path);
	ss << f.rdbuf();
	return parse_workload(ss.str(), seed);
}
