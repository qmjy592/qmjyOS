# Physical page allocator host tests

This optional host-side harness exercises `src/memory.c` without booting QEMU. It maps a small fake Multiboot info block and memory-map buffer at low addresses so the kernel code's 32-bit physical-address interface can be tested by a Linux host process.

From the project root, run:

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -I. src/memory.c tests/memory_host_test.c -o /tmp/qmjyOS-memory-test
/tmp/qmjyOS-memory-test
```

Optional undefined-behavior check:

```sh
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
  -fno-sanitize-recover=all -I. src/memory.c tests/memory_host_test.c \
  -o /tmp/qmjyOS-memory-test-ubsan
/tmp/qmjyOS-memory-test-ubsan
```

The harness returns exit code 77 with a `SKIP` message if the host does not permit the requested low-address mappings. Host tests do not replace a QEMU boot test.
