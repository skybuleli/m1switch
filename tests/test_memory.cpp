// ── Memory tests ────────────────────────────────────────────

#include "memory/Memory.h"

TEST(Memory_MapReadWrite) {
    Memory mem;

    // Allocate a page
    CHECK_EQ(Result::Success, mem.MapPhysical(0x40000000, 0x1000,
                                                Memory::Permission::RW));

    // Write and read back
    CHECK_EQ(Result::Success, mem.Write<u32>(0x40000000, 0xDEADBEEF));
    u32 val = 0;
    CHECK_EQ(Result::Success, mem.Read<u32>(0x40000000, &val));
    CHECK_EQ(0xDEADBEEF, val);

    return true;
}

TEST(Memory_StackHeap) {
    Memory mem;

    CHECK_EQ(Result::Success, mem.SetupStack(0x100000));
    CHECK(mem.GetStackTop() > 0);

    CHECK_EQ(Result::Success, mem.SetHeapSize(0x100000));
    CHECK_EQ(0x100000, mem.GetHeapSize());
    CHECK(mem.GetHeapBase() > 0);

    return true;
}

TEST(Memory_Permissions) {
    Memory mem;

    // RX page (code)
    CHECK_EQ(Result::Success, mem.MapPhysical(0x40000000, 0x1000,
                                                Memory::Permission::RX));

    // Should be able to read
    u8 val = 0;
    CHECK_EQ(Result::Success, mem.Read<u8>(0x40000000, &val));

    return true;
}
