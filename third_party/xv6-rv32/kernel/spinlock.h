// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};

struct rwspinlock {
  struct spinlock lock;
  uint readers;
  uint writer;
  uint pending_writers;
  char *name;
  struct cpu *cpu;
};
