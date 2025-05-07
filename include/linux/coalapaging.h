#ifndef __LINUX_COALAPAGING_H
#define __LINUX_COALAPAGING_H

#include <linux/mm_types.h>
#include <linux/mmzone.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/compaction.h>

#include "../../mm/coalapaging/internal.h"

/* enable COALAPaging allocations for page-cache */
extern bool coala_pagecache;

/* COALApaging heap allocation */
struct page *coala_alloc_pages_vma(gfp_t gfp, unsigned long addr,
		struct vm_area_struct *vma, int order, int node,
		int preferred_nid, nodemask_t *nmask);
/* COALAPaging folio allocation for pagecache readahead */
struct folio *coala_filemap_alloc_folio(gfp_t gfp, int order,
		struct address_space *mapping, pgoff_t offset);

static inline bool coala_is_filemap_alloc(struct task_struct *task) {
	return coala_pagecache && (task && task->mm && task->mm->coalapaging);
}

static inline bool coala_is_vma_alloc(struct vm_area_struct *vma) {
	return vma && ((vma->vm_mm && vma->vm_mm->coalapaging) || (vma->vm_flags & VM_COALA));
}

/* Decide if we want to do a THP allocation or fall back to 4K */
static inline bool coala_skip_thp(struct vm_area_struct *vma, unsigned long haddr) {
	//unsigned long fmfi;
	struct coala_hint hint;

	if (!coala_is_vma_alloc(vma)) {
		return false;
	}

	if (!coala_fault_hints || !coala_hints_enabled(vma->vm_mm)) {
		return false;
	}

	hint = coala_get_hint(vma->vm_mm, haddr);

#if 0
	if (!hint.val) {
		fmfi = extfrag_for_order(&(NODE_DATA(numa_node_id())->node_zones[ZONE_NORMAL]),
				HPAGE_PMD_ORDER);
		return fmfi >= coala_frag_thresh;
	}
#endif

	if (hint.promote) {
		return false;
	}

	return (hint.val == ULONG_MAX) || (hint.val < HPAGE_PMD_ORDER);
}

extern const struct file_operations coala_proc_ops;

int coala_init_hints(struct mm_struct *mm);
void coala_dup_hints(struct mm_struct *mm, struct mm_struct *oldmm);
void coala_drop_hints(struct mm_struct *mm);

int coala_madvise_hint(struct mm_struct *mm, unsigned long start,
		unsigned long end, unsigned long hint);

struct page *coala_khugepaged_alloc_page(int node, gfp_t gfp,
		unsigned long addr, struct mm_struct *mm);

#endif /* __LINUX_COALAPAGING_H */
