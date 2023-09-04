

#define EMULATED_CACHE_LINE_SIZE 16
#define SHARED_MEM_ADDR 0x20070000
#define SHARED_MEM_SIZE 0x00008000
#define RANDOM_CACHE_OPS 20

//---------------------------------------------------------------------

#undef Z_SPSC_PBUF_DCACHE_LINE
#define Z_SPSC_PBUF_DCACHE_LINE EMULATED_CACHE_LINE_SIZE

#undef CONFIG_IPC_SERVICE_BACKEND_ICMSG_WITH_BUF_CACHE_ALIGN
#define CONFIG_IPC_SERVICE_BACKEND_ICMSG_WITH_BUF_CACHE_ALIGN 1

#define sys_cache_data_invd_range(ptr, size) emu_invd_range(ptr, size)
#define sys_cache_data_flush_range(ptr, size) emu_flush_range(ptr, size)

#undef DT_INST_PHANDLE
#define DT_INST_PHANDLE(i, ...) (i & 1)

#undef DT_REG_SIZE
#undef DT_REG_ADDR
#define DT_REG_SIZE(x) (SHARED_MEM_SIZE / 2)
#define DT_REG_ADDR(x) ((intptr_t)(void*)(&emu_shmem) + (x) * DT_REG_SIZE(x))

static uint8_t emu_shmem[SHARED_MEM_SIZE] __attribute__ ((aligned (EMULATED_CACHE_LINE_SIZE)));;

static void emu_invd_range(const void* ptr, size_t size)
{
	uintptr_t start = (uint8_t*)(ptr) - emu_shmem;
	uintptr_t end = start + size;
	start = ROUND_DOWN(start, EMULATED_CACHE_LINE_SIZE);
	end = ROUND_UP(end, EMULATED_CACHE_LINE_SIZE);
	memcpy(emu_shmem + start, (uint8_t*)SHARED_MEM_ADDR + start, end - start);
}

static void emu_flush_range(const void* ptr, size_t size)
{
	uintptr_t start = (uint8_t*)(ptr) - emu_shmem;
	uintptr_t end = start + size;
	start = ROUND_DOWN(start, EMULATED_CACHE_LINE_SIZE);
	end = ROUND_UP(end, EMULATED_CACHE_LINE_SIZE);
	memcpy((uint8_t*)SHARED_MEM_ADDR + start, emu_shmem + start, end - start);
}

static void random_cache_oper()
{
	size_t lines = SHARED_MEM_SIZE / EMULATED_CACHE_LINE_SIZE;
	for (int i = 0; i < RANDOM_CACHE_OPS; i++) {
		size_t line = rand() % lines;
		ptr = emu_shmem + line * EMULATED_CACHE_LINE_SIZE;
		if (rand() & 1) {
			emu_invd_range(ptr, EMULATED_CACHE_LINE_SIZE);
		} else {
			emu_flush_range(ptr, EMULATED_CACHE_LINE_SIZE);
		}
	}
}

