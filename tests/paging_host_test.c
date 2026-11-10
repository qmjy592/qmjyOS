#include "paging.h"
#include <stdio.h>

int main(void) {
    int rc = vm_self_test();
    if (rc != VM_OK) {
        fprintf(stderr, "Paging self-test FAILED: %d\n", rc);
        return 1;
    }
    puts("Paging self-test: PASS");
    return 0;
}
