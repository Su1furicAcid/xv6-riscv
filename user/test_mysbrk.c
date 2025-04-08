#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void)
{
    printf("=== Testing mysbrk ===\n");

    // 获取当前的堆顶地址
    uint64 initial_brk = mysbrk(0); // mysbrk 返回 uint64
    printf("Initial program break: 0x%lx\n", initial_brk);

    // 增加堆大小
    printf("Expanding heap by 64 bytes...\n");
    uint64 new_brk = mysbrk(64); // mysbrk 返回 uint64
    uint64 new_new_brk = mysbrk(64); // mysbrk 返回 uint64
    printf("New program break after expansion: 0x%lx\n", new_new_brk);
    if (new_new_brk == (uint64)-1) {
        printf("mysbrk failed to expand heap\n");
        exit(1);
    }
    printf("New program break: 0x%lx\n", mysbrk(0));

    // 写入数据到新分配的内存
    printf("Writing data to the newly allocated memory...\n");
    char *allocated_memory = (char *)new_brk; // 将 uint64 转换为指针
    for (int i = 0; i < 128; i++) {
        allocated_memory[i] = (char)(i % 256);
    }
    printf("Data written successfully.\n");

    // 验证数据
    printf("Verifying written data...\n");
    for (int i = 0; i < 128; i++) {
        if (allocated_memory[i] != (char)(i % 256)) {
            printf("Data verification failed at offset %d\n", i);
            exit(1);
        }
    }
    printf("Data verification succeeded.\n");

    // 减少堆大小
    printf("Shrinking heap by 128 bytes...\n");
    if (mysbrk(-128) == (uint64)-1) {
        printf("mysbrk failed to shrink heap\n");
        exit(1);
    }
    printf("Heap successfully shrunk. Current program break: 0x%lx\n", mysbrk(0));
    printf("=== mysbrk test completed ===\n");
    exit(0);
}