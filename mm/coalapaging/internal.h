#ifndef __LINUX_COALAPAGING_INTERNAL_H
#define __LINUX_COALAPAGING_INTERNAL_H

#include "../internal.h"

/* enable COALAPaging allocations for the page-cache */
extern bool coala_pagecache;
/* Fragmentation threashold for default THP allocations */
extern unsigned long coala_frag_thresh;
/* enable stats */
extern bool coala_stats;
/* allow khugepaged to defrag COALA-enabled mm's */
extern bool coala_khugepaged;
/* enable / disable kcompactd */
extern bool coala_kcompactd;
/* enable / disable hints at fault */
extern bool coala_fault_hints;
/* fallback to normal khugepaged when no khuge hints active */
extern bool coala_khuge_fallback;
/* async promotion to 32M */
extern bool coala_khuge_etheap_async;
/* khuge task round-robin */
extern bool coala_khuge_rr;

/* hijack pr_fmt */
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "(%s): " fmt, __func__

#ifndef ALIGN
#define __ALIGN_MASK(x,mask)    (((x)+(mask))&~(mask))
#define ALIGN(x,a)              __ALIGN_MASK(x,(typeof(x))(a)-1)
#endif

/* helper to check if a page is part of a range */
#define within(page, start, len) (((page) >= (start)) && \
		((page) < ((start) + (len))))

/* Fallback defs */
#ifndef CONT_PTES
#define CONT_PTES		1
#endif

#ifndef CONT_PTE_SIZE
#define CONT_PTE_SIZE		(CONT_PTES * PAGE_SIZE)
#endif

#ifndef CONT_PTE_MASK
#define CONT_PTE_MASK		(~(CONT_PTE_SIZE - 1))
#endif

#ifndef CONT_PMDS
#define CONT_PMDS		1
#endif

#ifndef CONT_PMD_SIZE
#define CONT_PMD_SIZE		(CONT_PMDS * PMD_SIZE)
#endif

#ifndef CONT_PMD_MASK
#define CONT_PMD_MASK		(~(CONT_PMD_SIZE - 1))
#endif

#define COALA_HINT_64K	4
#define COALA_HINT_2M	9
#define COALA_HINT_32M	13

#define COALA_HINT_IDXSZ	(64 << 10)

/*
 * COALAPaging request, includes the nodemask pointer, because we hijack
 * the nodeamsk pointer parameter passed to __alloc_pages()
 */
struct coalareq {
	/* target vaddr for the allocation */
	unsigned long vaddr;

	/* for a pagecache request */
	struct address_space *mapping;

	/* mm struct for the current allocation */
	struct mm_struct *mm;

	/* saved nodemask pointer */
	nodemask_t *nmask;
};
#define COALAREQ_BIT 63

/* 'pack' the request in the nodemask pointer */
static inline nodemask_t *coalareq_to_nmask(nodemask_t *nmask, struct coalareq *req) {
	req->nmask = nmask;
	/* FIXME: ugly hack, clear msb to mark this as a coala allocation */
	return (nodemask_t *)((uintptr_t)req & ((1UL << COALAREQ_BIT) - 1));
}

/* 'unpack' the coala alloc request from the nodemask */
static inline nodemask_t *coalareq_from_nmask(nodemask_t *nmask, struct coalareq **req) {
	/* ugly hack to hijack nmask, we clear the msb, making it a 'userspace' pointer */
	if (!nmask || ((uintptr_t)nmask & (1UL << COALAREQ_BIT))) {
		/* this is a kernel pointer, return back the nodemask */
		return nmask;
	}

	*req = (struct coalareq *)((uintptr_t)nmask |  (1UL << COALAREQ_BIT));
	return (*req)->nmask;
}

/* check if a pfn is a valid candidate for buddy neighbour */
static inline bool is_valid_buddy(unsigned long pfn, struct zone *zone,
		int migratetype, int order) {
	struct page *page;

	if (pfn_valid(pfn)) {
		page = pfn_to_page(pfn);

		if (!page_is_guard(page) && PageBuddy(page) &&
			buddy_order(page) == order && page_zone(page) == zone &&
			get_pageblock_migratetype(page) == migratetype) {
			return true;
		}
	}

	return false;
}

struct coala_hint {
	unsigned long val;
	bool khugepaged_mark;
	bool promote;
};

static inline bool coala_hints_enabled(struct mm_struct *mm) {
	return !xa_empty(mm->coala_hints.hints);
}

static inline size_t coala_hints_khuge(struct mm_struct *mm) {
	size_t ret = 0;
	unsigned long index;
	void *entry;

	xa_for_each_marked(mm->coala_hints.hints, index, entry, XA_MARK_1) {
		ret++;
	}

	return ret;
}

static inline struct coala_hint coala_get_hint(struct mm_struct *mm,
		unsigned long addr) {
	void *entry;
	unsigned long vpn = addr >> PAGE_SHIFT;
	struct coala_hint result = { .val = ULONG_MAX, .khugepaged_mark = false,
		.promote = false };

	entry = xa_load(mm->coala_hints.hints, vpn);
	if (!entry || !xa_is_value(entry)) {
		return result;
	}

	result.val = xa_to_value(entry);
	result.khugepaged_mark = xa_get_mark(mm->coala_hints.hints, vpn, XA_MARK_1);
	result.promote = xa_get_mark(mm->coala_hints.hints, vpn, XA_MARK_2);

	return result;
}

extern atomic64_t coala_hints_active;

static inline void coala_clear_khugepaged_mark(struct mm_struct *mm,
		unsigned long addr, unsigned long val) {
	struct coala_hint hint;

	hint = coala_get_hint(mm, addr);

	if (hint.val != val) {
		return;
	}

	if (!hint.khugepaged_mark) {
		return;
	}

	pr_debug("clearing %lu-order hint", val);
	xa_clear_mark(mm->coala_hints.hints, addr >> PAGE_SHIFT, XA_MARK_1);

	switch (val) {
	case COALA_HINT_64K:
		WARN_ON(!atomic64_fetch_dec_relaxed(&mm->coala_hints.contptes));
		break;
	case COALA_HINT_2M:
		WARN_ON(!atomic64_fetch_dec_relaxed(&mm->coala_hints.pmds));
		break;
	case COALA_HINT_32M:
		WARN_ON(!atomic64_fetch_dec_relaxed(&mm->coala_hints.contpmds));
		break;
	default:
		break;
	}

#if 0
	if (!xa_marked(mm->coala_hints.hints, XA_MARK_1)) {
		atomic64_dec(&coala_hints_active);
	}
#endif

	pr_debug("remaining conptes: %llu, pmds: %llu, contpmds: %llu",
			atomic64_read(&mm->coala_hints.contptes),
			atomic64_read(&mm->coala_hints.pmds),
			atomic64_read(&mm->coala_hints.contpmds));
	//pr_debug("remaining khuge hints: %lu", coala_hints_khuge(mm));
}
#endif /* __LINUX_COALAPAGING_INTERNAL_H */
