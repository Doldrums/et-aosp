# SPDX-License-Identifier: GPL-2.0
obj-y				:= dma-mapping.o extable.o fault.o init.o \
				   cache.o copypage.o flush.o \
				   ioremap.o mem_encrypt.o mmap.o pgd.o mmu.o \
				   context.o proc.o pageattr.o
obj-$(CONFIG_MEMORY_RELINQUISH)	+= mem_relinquish.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_PTDUMP_CORE)	+= ptdump.o
obj-$(CONFIG_PTDUMP_DEBUGFS)	+= ptdump_debugfs.o
obj-$(CONFIG_TRANS_TABLE)	+= trans_pgd.o
obj-$(CONFIG_TRANS_TABLE)	+= trans_pgd-asm.o
obj-$(CONFIG_DEBUG_VIRTUAL)	+= physaddr.o
obj-$(CONFIG_ARM64_MTE)		+= mteswap.o
KASAN_SANITIZE_physaddr.o	+= n

obj-$(CONFIG_KASAN)		+= kasan_init.o
KASAN_SANITIZE_kasan_init.o	:= n

obj-$(CONFIG_ARM64_ELASTIC_TRANSLATIONS) += et.o
obj-$(CONFIG_PFTRACE) += pftrace.o
