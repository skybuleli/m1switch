#include "kernel/Kernel.h"

KHandleTable& KernelHandleTable() {
    static KHandleTable table;
    return table;
}
