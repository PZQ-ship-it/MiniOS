#include "printk.h"
#include "sbi.h"
#include "proc.h"
extern void test();

int start_kernel() {

    schedule();
    test(); // DO NOT DELETE !!!

	return 0;
}
