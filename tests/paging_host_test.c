#include "paging.h"
#include <assert.h>
#include <stdio.h>

static void test_independent_address_spaces(void)
{
    vm_address_space_t first = {0};
    vm_address_space_t second = {0};
    uint64_t pa_first = 0, pa_second = 0, flags_first = 0, flags_second = 0;
    size_t tables_before = vm_tables_used();

    assert(vm_address_space_create(&first) == VM_OK);
    assert(vm_address_space_create(&second) == VM_OK);
    assert(first.root_physical != second.root_physical);

    /* The same user VA must resolve to different physical pages per process. */
    assert(vm_address_space_map_page(&first, VM_USER_MIN_VA,
                                     0x30000000ULL,
                                     VM_USER) == VM_OK);
    assert(vm_address_space_map_page(&second, VM_USER_MIN_VA,
                                     0x31000000ULL,
                                     VM_USER | VM_WRITE | VM_NO_EXECUTE) == VM_OK);
    assert(vm_address_space_query_page(&first, VM_USER_MIN_VA,
                                       &pa_first, &flags_first) == VM_OK);
    assert(vm_address_space_query_page(&second, VM_USER_MIN_VA,
                                       &pa_second, &flags_second) == VM_OK);
    assert(pa_first == 0x30000000ULL);
    assert(pa_second == 0x31000000ULL);
    assert((flags_first & VM_USER) != 0);
    assert((flags_first & VM_WRITE) == 0);
    assert((flags_second & (VM_USER | VM_WRITE | VM_NO_EXECUTE)) ==
           (VM_USER | VM_WRITE | VM_NO_EXECUTE));

    /* A top-of-user-range stack address must use and later free PML4[255]. */
    assert(vm_address_space_map_page(&first, VM_USER_MAX_VA - VM_PAGE_SIZE,
                                     0x33000000ULL,
                                     VM_USER | VM_WRITE | VM_NO_EXECUTE) == VM_OK);
    assert(vm_address_space_query_page(&first, VM_USER_MAX_VA - VM_PAGE_SIZE,
                                       &pa_first, &flags_first) == VM_OK);
    assert(pa_first == 0x33000000ULL);
    assert(vm_address_space_query_page(&second, VM_USER_MAX_VA - VM_PAGE_SIZE,
                                       &pa_second, &flags_second) == VM_ERR_NOT_MAPPED);

    /* User mapping APIs cannot replace the shared kernel identity map. */
    assert(vm_address_space_map_page(&first, 0x00200000ULL,
                                     0x32000000ULL, VM_USER) == VM_ERR_ADDRESS);
    assert(vm_address_space_query_page(&first, 0x00201234ULL,
                                       &pa_first, &flags_first) == VM_OK);
    assert(pa_first == 0x00201234ULL);
    assert((flags_first & VM_USER) == 0);

    /* Exercise real root selection in the host model without privileged CR3. */
    assert(vm_address_space_activate(&first) == VM_OK);
    assert(vm_current_root_physical() == first.root_physical);
    assert(vm_address_space_activate(&second) == VM_OK);
    assert(vm_current_root_physical() == second.root_physical);
    assert(vm_activate() == VM_OK);
    assert(vm_current_root_physical() == vm_root_physical());

    assert(vm_address_space_destroy(&first) == VM_OK);
    assert(vm_address_space_destroy(&second) == VM_OK);
    assert(first.magic == 0 && second.magic == 0);
    assert(vm_tables_used() == tables_before);
}

int main(void) {
    int rc = vm_self_test();
    if (rc != VM_OK) {
        fprintf(stderr, "Paging self-test FAILED: %d\n", rc);
        return 1;
    }
    test_independent_address_spaces();
    puts("Paging self-test and isolated address-space tests: PASS");
    return 0;
}
