# socsim — a multi-core SoC simulator: scheduling, virtual memory and caches

[![CI](https://github.com/YOUR_GITHUB_USERNAME/socsim/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_GITHUB_USERNAME/socsim/actions/workflows/ci.yml)

socsim simulates what happens inside a multi-core chip (a system-on-chip, like the ones in
phones, cars and NVIDIA Jetson boards) when several programs run at once:

* the **OS scheduler** decides which process runs on which CPU core (FCFS, SJF, Round Robin, Priority);
* every memory access goes through the **MMU**: a per-core **TLB**, then the **page table**,
  and on a **page fault** a page is loaded and another one evicted (FIFO, LRU or Clock);
* the data then comes from a per-core **L1 cache**, a shared **L2 cache**, or DRAM.

It is about 1,300 lines of C++17 with no dependencies. Every algorithm is checked against the worked
examples in the standard OS textbook (Silberschatz, *Operating System Concepts*), and two
experiments show how the pieces interact.

```
   +-------- core 0 --------+   +-------- core 1 --------+
   | running process        |   | running process        |   <- scheduler picks who runs
   | TLB (32 entries)       |   | TLB (32 entries)       |   <- virtual -> physical cache
   | L1 cache (32 KiB)      |   | L1 cache (32 KiB)      |
   +-----------+------------+   +-----------+------------+
               +------------+---------------+
                     shared L2 cache (256 KiB)
                                |
          DRAM: 1024 physical frames of 4 KiB   <- page tables, page faults, replacement
```

## Two findings

**1. Flushing the TLB on every context switch is expensive when time slices are short.**
Every process uses the same virtual addresses, so the TLB must be emptied when the CPU switches
process — unless each entry is tagged with an *address-space ID* (ASID), as ARM and x86 (PCID) do.
With 100-tick slices, flushing drops the TLB hit rate from 66% to 36%, and ASID tags cut the total
run time by 17%; with long slices the difference disappears.

![TLB hit rate vs time slice](docs/images/tlb_asid.png)

**2. Too little memory makes the system thrash.** Below about 275 frames the processes keep
evicting each other's pages: page faults rise 200-fold and CPU utilization falls from 91% to under 2%,
because every process is waiting for the disk. LRU and Clock need up to 12× fewer faults than FIFO;
Clock gets almost LRU's result while needing only one "referenced" bit per page, which is why
real kernels use it.

![Thrashing](docs/images/thrashing.png)

Also measured: 2 cores finish the workload 1.91× faster than 1; 4 cores only 2.4× faster,
because 5 processes cannot keep 4 cores busy once the short jobs finish (utilization 51%).

## How it maps to the System Software Engineer (GPU/Tegra) job description

| Job description topic | Where in socsim |
|---|---|
| CPU and memory architecture | `cache.cpp` set-associative L1/L2 caches with LRU; latencies L1 1, L2 10, DRAM 100 ticks; AMAT reported |
| Multi-processing and scheduling | `scheduler.cpp` FCFS, SJF, Round Robin, preemptive Priority on N cores; context-switch cost; Gantt chart |
| Memory management | `memory.cpp` per-process page tables, page faults, global replacement (FIFO, LRU, Clock), memory freed on exit |
| Hardware/OS interface | `tlb.cpp` TLB with flush vs ASID tags; TLB shootdown on every core when a page is evicted |
| Algorithms and data structures | ring of frames with a clock hand, LRU via timestamps, ready queue (deque), set-associative indexing |
| Object-oriented C++ | policies are classes behind abstract interfaces (`Scheduler`, `ReplacementPolicy`) — the strategy pattern |
| Python | `tools/experiments.py` runs parameter sweeps, writes CSVs and charts |

Why it matters for GPU and Tegra system software: Tegra chips are multi-core Arm SoCs running
Linux or QNX, and GPU drivers solve the same problems for the GPU — they manage page tables and
TLB invalidation for each process's GPU memory, and time-slice work from many applications.

## Build and run

Linux, macOS or WSL2 with `g++` (or clang) and `make`:

```bash
make                      # builds build/socsim and build/tests
make test                 # 15 tests, 63 checks (textbook examples included)
make asan                 # same tests under AddressSanitizer + UndefinedBehaviorSanitizer
make demo                 # textbook FCFS and Round Robin with Gantt charts, then the mixed workload

./build/socsim workloads/mixed.txt                        # default: 2 cores, RR, Clock, 4 MiB RAM
./build/socsim workloads/mixed.txt --frames 250           # watch it thrash
./build/socsim workloads/mixed.txt --quantum 100 --asid   # short slices, tagged TLB
./build/socsim workloads/textbook_priority.txt --ideal --cores 1 --cs 0 --sched prio --gantt
./build/socsim --help

pip install matplotlib && python3 tools/experiments.py    # re-run both experiments + charts
```

Example (`make demo`, Round Robin with quantum 4 — the textbook answer is an average wait of 5.67):

```
process       arrive     burst  prio    finish  turnaround   waiting  response  faults
P1                 0        24     0        30          30         6         0       0
P2                 0         3     0         7           7         4         4       0
P3                 0         3     0        10          10         7         7       0
average                                               15.7       5.7       3.7

core 0: | P1 0-4 | P2 4-7 | P3 7-10 | P1 10-30 |
```

## Workload files

One process per line: `name arrival burst priority pattern wss_kb`

| pattern | behaviour | stresses |
|---|---|---|
| `seq` | reads its data word after word, like a loop over an array | best case for caches |
| `random` | jumps anywhere in its working set (like a database) | caches and TLB |
| `hotcold` | 90% of accesses go to 10% of its data (typical of real programs) | LRU vs FIFO |
| `stride` | touches a new page on every access (like a backup scan) | worst case for TLB and memory |
| `-` | no memory model, for textbook scheduling examples (use `--ideal`) | scheduling only |

## Project layout

```
src/config.h          all parameters and their defaults
src/cache.*           set-associative LRU cache (used for L1 and L2)
src/tlb.*             TLB: lookup, insert, flush, shootdown
src/memory.*          page tables, frames, FIFO / LRU / Clock replacement
src/process.*         workload parsing and memory-access patterns
src/scheduler.*       FCFS / SJF / Round Robin / Priority
src/simulator.*       the cores and the simulation loop, statistics, report
src/main.cpp          command line
tests/tests.cpp       unit tests incl. textbook examples and Belady's anomaly
workloads/            textbook examples + a realistic mixed workload
tools/experiments.py  experiments and charts
docs/HOW_IT_WORKS.md  plain-language walkthrough and interview questions
```

## Simplifications (and how real systems differ)

* Time is counted in ticks; the time slice and page-fault time are scaled down from real
  milliseconds so a run takes under a second. Trends, not absolute numbers, are the point.
* One memory access is one unit of work; a faulting access completes once the page is loaded.
* Exact LRU timestamps every access, which no hardware does — that is why Clock exists.
* Single-level page tables (real ones have 2–5 levels); caches do not model writes or coherence.
* Ideas for extensions: multilevel feedback queue scheduling, a working-set or page-fault-frequency
  policy to stop thrashing, set-associative TLBs, cache coherence (MESI) between cores.
