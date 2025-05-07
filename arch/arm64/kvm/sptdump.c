// SPDX-License-Identifier: GPL-2.0-only
//
#include <linux/kvm_host.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <asm/kvm_pgtable.h>
#include <linux/mmap_lock.h>
#include <linux/srcu.h>
#include <linux/align.h>
#include <asm/pgtable-hwdef.h>
#include <vdso/bits.h>

static ssize_t sptdump_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	struct kvm *kvm= file->f_inode->i_private;
	struct kvm_pgtable *pgt = kvm->arch.mmu.pgt;
	kvm_pte_t pte = 0;
	gfn_t gfn;
	u32 level = ~0;
	int idx, bkt, n, ret = 0;
	size_t coppied = 0;
	char line[128];

	idx = srcu_read_lock(&kvm->srcu);
	mmap_read_lock(kvm->mm);
	read_lock(&kvm->mmu_lock);

	gfn = *ppos;

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, bkt, slots) {
		if (gfn >= memslot->base_gfn + memslot->npages) {
			continue;
		}
		gfn = max((unsigned long)gfn, (unsigned long)memslot->base_gfn);

		while (count > 0 && gfn < memslot->base_gfn + memslot->npages) {
			if (kvm_pgtable_get_leaf(pgt, gfn << PAGE_SHIFT, &pte, &level)) {
				gfn++; 
				continue;
			}

			n = snprintf(line, 512, "gfn=0x%llx, pte=0x%llx, level=%u\n",
					gfn, pte, level);
			if (count < n) {
				goto out_fin;
			}

			ret = copy_to_user(buf, line, n);
			if (ret) {
				ret = -EFAULT;
				goto out;
			}

			buf += n;
			coppied += n;
			count -= n;

			gfn += BIT(ARM64_HW_PGTABLE_LEVEL_SHIFT(level) - PAGE_SHIFT);
		}

		if (!count) {
			goto out_fin;
		}
	}

out_fin:
	*ppos = gfn;
	ret = coppied;

out:
	read_unlock(&kvm->mmu_lock);
	mmap_read_unlock(kvm->mm);
	srcu_read_unlock(&kvm->srcu, idx);

	return ret;
}

loff_t sptdump_lseek(struct file *file, loff_t offset, int orig)
{
	switch (orig) {
	case 0:
		file->f_pos = offset;
		break;
	case 1:
		file->f_pos += offset;
		break;
	default:
		return -EINVAL;
	}
	return file->f_pos;
}

const struct file_operations sptdump_fops = {
	.llseek		= sptdump_lseek,
	.read		= sptdump_read,
};

int kvm_arch_create_vm_debugfs(struct kvm *kvm)
{
	debugfs_create_file("sptdump", 0444, kvm->debugfs_dentry, kvm, &sptdump_fops);
	return 0;
}
