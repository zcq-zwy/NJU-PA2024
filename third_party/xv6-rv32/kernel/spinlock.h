// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
  int nts;           // #test-and-set failures.
  int n;             // #acquire() calls.
};

struct rwspinlock {
  struct spinlock lock;
  uint readers;
  uint writer;
  uint waiting_writers;
  struct cpu *cpu;
};
