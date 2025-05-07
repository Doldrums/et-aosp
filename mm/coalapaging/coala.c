/* COALApaging core */

#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mempolicy.h>
#include <linux/printk.h>
#include <linux/compaction.h>
#include <linux/xarray.h>
#include <linux/moduleparam.h>

#include <asm-generic/mman-common.h>

#include "../internal.h"
#include "../../fs/proc/internal.h"

#include <linux/coalapaging.h>
#include "internal.h"
#include "stats.h"

#ifdef CONFIG_PFTRACE
#include <linux/tracepoint-defs.h>

DECLARE_TRACEPOINT(coala_allocpages);
void do_trace_coala_allocpages(unsigned long cycles);
#endif /* CONFIG_PFTRACE */

atomic64_t coala_hints_active;

static ssize_t coala_proc_read(struct file *file, char __user *buf,
                       size_t count, loff_t *ppos) {
	struct task_struct *task = get_proc_task(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;
	int ret;

	if (!task)
		return -ESRCH;

	BUG_ON(!task->mm);

	mmap_read_lock(task->mm);
	ret = task->mm->coalapaging;
	mmap_read_unlock(task->mm);

	put_task_struct(task);
	len = snprintf(buffer, sizeof(buffer), "%hd\n", ret);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct file_operations coala_proc_ops = {
	.read           = coala_proc_read,
	.llseek         = default_llseek,
};

#define BUFLEN (1UL << 22)

static ssize_t coala_hints_proc_read(struct file *file, char __user *buf,
                       size_t count, loff_t *ppos) {
	struct task_struct *task = get_proc_task(file_inode(file));
	char *buffer, line[256];
	ssize_t len, ret = 0, maxlen = min(count, BUFLEN);
	void *entry;
	unsigned long idx;

	if (!task) {
		return -ESRCH;
	}

	BUG_ON(!task->mm);
	
	if (*ppos == 0xff) {
		return 0;
	}

	buffer = vmalloc(maxlen);
	if (!buffer) {
		return -ENOMEM;
	}

	mmap_read_lock(task->mm);

	idx = *ppos;

	xa_lock((struct xarray *)task->mm->coala_hints.hints);
	xa_for_each_start(task->mm->coala_hints.hints, idx, entry, idx) {
		if (!entry || !xa_is_value(entry)) {
			continue;
		}

		len = snprintf(line, sizeof(line), "%10lx %2ld\n", idx,
				xa_to_value(entry));
		if (ret + len > maxlen) {
			*ppos = idx;
			break;
		}

		memcpy(buffer + ret, line, len);
		ret += len;
	}

	if (!entry) {
		*ppos = 0xff;
	}

	xa_unlock((struct xarray *)task->mm->coala_hints.hints);
	mmap_read_unlock(task->mm);

	if (copy_to_user(buf, buffer, ret)) {
		ret = -EFAULT;
	}

	vfree(buffer);
	put_task_struct(task);

	return ret;
}

static ssize_t coala_hints_proc_write(struct file *file, const char __user *buf,
                       size_t count, loff_t *ppos) {
	struct task_struct *task = get_proc_task(file_inode(file));
	void *entry;

	if (!task) {
		return -ESRCH;
	}

	BUG_ON(!task->mm);

	XA_STATE(xas, task->mm->coala_hints.hints, 0);

	mmap_write_lock(task->mm);

	xa_lock((struct xarray *)task->mm->coala_hints.hints);

	if (!xa_empty(task->mm->coala_hints.hints)) {
		atomic64_dec(&coala_hints_active);
	}

	xas_for_each(&xas, entry, ULONG_MAX) {
		if (!entry || !xa_is_value(entry)) {
			continue;
		}

		xas_init_marks(&xas);
		xas_store(&xas, NULL);
	}

	atomic64_set(&task->mm->coala_hints.contptes, 0);
	atomic64_set(&task->mm->coala_hints.pmds, 0);
	atomic64_set(&task->mm->coala_hints.contpmds, 0);

	memset(task->mm->coala_hints.index, 0, sizeof(unsigned long) * COALA_HINT_IDXSZ);

	task->mm->coala_hints.epoch += 1;

	xa_unlock((struct xarray *)task->mm->coala_hints.hints);
	mmap_write_unlock(task->mm);

	put_task_struct(task);
	return count;
}

const struct file_operations coala_hints_proc_ops = {
	.read           = coala_hints_proc_read,
	.write			= coala_hints_proc_write,
	.llseek         = default_llseek,
};

int coala_madvise_hint(struct mm_struct *mm, unsigned long start,
		unsigned long end, unsigned long hint) {
	unsigned long order, order_nrpages, i;
	bool empty;
	struct coala_hint _hint;
	int ret = 0;

	XA_STATE(xas, mm->coala_hints.hints, 0);

	xa_lock((struct xarray *)mm->coala_hints.hints);

	empty = xa_empty(mm->coala_hints.hints);

	_hint = coala_get_hint(mm, start);
	start >>= PAGE_SHIFT;

	switch (hint) {
	case MADV_COALA_HINT_64K:
		order = COALA_HINT_64K;
		break;
	case MADV_COALA_HINT_2M:
		order = COALA_HINT_2M;
		break;
	case MADV_COALA_HINT_32M:
		order = COALA_HINT_32M;
		break;
	case MADV_COALA_HINT_KHUGE:
		if (_hint.val == ULONG_MAX) {
			pr_debug("cannot find hint for 0x%lx", start);
			ret = -ENOENT;
			goto out;
		}

		if (_hint.khugepaged_mark) {
			ret = -EEXIST;
			goto out;
		}

		for (i = 0; i < COALA_HINT_IDXSZ; i++) {
			BUG_ON(mm->coala_hints.index[i] == start);
			if (!mm->coala_hints.index[i]) {
				mm->coala_hints.index[i] = start;
				break;
			}
		}
		BUG_ON(i >= COALA_HINT_IDXSZ);

#if 0
		if (coala_hints_khuge(mm) > 10) {
			pr_debug("waiting for %lu hints to be processed",
					coala_hints_khuge(mm));
			return -EAGAIN;
		}
#endif

		pr_debug("setting mark for 0x%lx order-%lu", start, _hint.val);
		xas_set_order(&xas, start, _hint.val);
		xas_load(&xas);
		xas_set_mark(&xas, XA_MARK_1);

		goto out;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (_hint.val != ULONG_MAX) {
		pr_debug("eexist");
		ret = -EEXIST;
		goto out;
	}

	order_nrpages = 1UL << order;
	if (!IS_ALIGNED(start, order_nrpages)) {
		pr_debug("einval");
		ret = -EINVAL;
		goto out;
	}

	xas_set_order(&xas, start, order);
	xas_store(&xas, xa_mk_value(order));

	if (empty) {
		atomic64_inc(&coala_hints_active);
	}

	switch (hint) {
	case MADV_COALA_HINT_64K:
		atomic64_inc(&mm->coala_hints.contptes);
		break;
	case MADV_COALA_HINT_2M:
		atomic64_inc(&mm->coala_hints.pmds);
		break;
	case MADV_COALA_HINT_32M:
		atomic64_inc(&mm->coala_hints.contpmds);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

out:
	xa_unlock((struct xarray *)mm->coala_hints.hints);
#if 0
	if (hint == MADV_COALA_HINT_KHUGE) {
		pr_debug("%lu hints set for khuge", coala_hints_khuge(mm));
	}
#endif
	return ret;
}

int coala_init_hints(struct mm_struct *mm) {
	mm->coala_hints.hints = kmalloc(sizeof(struct xarray), GFP_KERNEL);
	if (!mm->coala_hints.hints) {
		return -ENOMEM;
	}

	mm->coala_hints.index = kzalloc(sizeof(unsigned long) * COALA_HINT_IDXSZ,
			GFP_KERNEL);
	if (!mm->coala_hints.index) {
		xa_destroy(mm->coala_hints.hints);
		kfree(mm->coala_hints.hints);
		return -ENOMEM;
	}

	xa_init(mm->coala_hints.hints);

	atomic64_set(&mm->coala_hints.contptes, 0);
	atomic64_set(&mm->coala_hints.pmds, 0);
	atomic64_set(&mm->coala_hints.contpmds, 0);

	mm->coala_hints.epoch = 0;

	return 0;
}

void coala_dup_hints(struct mm_struct *mm, struct mm_struct *oldmm) {
	void *entry;
	unsigned long idx;

	XA_STATE(xas, mm->coala_hints.hints, 0);

	xa_for_each(oldmm->coala_hints.hints, idx, entry) {
		xas_set_order(&xas, idx, xa_get_order(oldmm->coala_hints.hints, idx));
		xas_store(&xas, entry);
		if (xa_get_mark(oldmm->coala_hints.hints, idx, XA_MARK_1)) {
			xas_set_mark(&xas, XA_MARK_1);
		}
	}

	if (!xa_empty(mm->coala_hints.hints)) {
		atomic64_inc(&coala_hints_active);
	}

	atomic64_set(&mm->coala_hints.contptes,
			atomic64_read(&oldmm->coala_hints.contptes));
	atomic64_set(&mm->coala_hints.pmds,
			atomic64_read(&oldmm->coala_hints.pmds));
	atomic64_set(&mm->coala_hints.contpmds,
			atomic64_read(&oldmm->coala_hints.contpmds));

	memcpy(mm->coala_hints.index, oldmm->coala_hints.index,
			sizeof(unsigned long) * COALA_HINT_IDXSZ);

	mm->coala_hints.epoch = oldmm->coala_hints.epoch;
}

void coala_drop_hints(struct mm_struct *mm) {
	if (!xa_empty(mm->coala_hints.hints)) {
		atomic64_dec(&coala_hints_active);
	}

	xa_destroy(mm->coala_hints.hints);
	kfree(mm->coala_hints.hints);
	kfree(mm->coala_hints.index);
}

struct page *coala_alloc_pages_vma(gfp_t gfp, unsigned long addr,
		struct vm_area_struct *vma, int order, int node,
		int preferred_nid, nodemask_t *nmask) {
	struct page *page;
	struct coalareq req;
	struct coala_hint hint;
#ifdef CONFIG_PFTRACE
	unsigned long cycles = get_cycles();
#endif /* CONFIG_PFTRACE */

	req.vaddr = addr;
	req.mm = vma->vm_mm;
	req.mapping = NULL;

	if (!coala_fault_hints || !coala_hints_enabled(vma->vm_mm)) {
		pr_debug("skipping coala req");
		goto setreq;
	}

	hint = coala_get_hint(vma->vm_mm, addr);
	if ((hint.val == ULONG_MAX) || (hint.val <= order)) {
		pr_debug("skipping coala req %lu %d", hint.val, order);
		goto out_alloc;
	}

setreq:
	/* embed the request in the nodemask */
	nmask = coalareq_to_nmask(nmask, &req);

out_alloc:
	page = __alloc_pages(gfp, order, preferred_nid, nmask);
#if 0
	if (page && coala_hints_enabled(vma->vm_mm) && order && (hint.val == order)) {
		pr_debug("faulted order-%lu hint", hint.val);
		coala_clear_khugepaged_mark(vma->vm_mm, addr, hint.val);
	}
#endif

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(coala_allocpages)) {
		do_trace_coala_allocpages(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */

	return page;
}

static inline struct folio *coala_folio_alloc(gfp_t gfp, int order,
		struct coalareq *req) {
	struct page *page = NULL;

	/* FIXME: this bypasses the NUMA paths in alloc_pages() */

	if (order) {
		gfp |= __GFP_COMP;
	}

	page = __alloc_pages(gfp, order, numa_node_id(), coalareq_to_nmask(NULL, req));

	if (page && order) {
		prep_transhuge_page(page);
	}

	return (struct folio *)page;
}

struct folio *coala_filemap_alloc_folio(gfp_t gfp, int order,
		struct address_space *mapping, pgoff_t offset) {
	struct coalareq req;

	req.vaddr = offset;
	req.mm = NULL;
	req.mapping = mapping;

	return coala_folio_alloc(gfp, order, &req);
}
