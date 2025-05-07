/* included in page_alloc.c */

#ifndef __LINUX_COALAPAGING_ALLOC_H
#define __LINUX_COALAPAGING_ALLOC_H

#include "internal.h"
#include "stats.h"
#include <linux/coalapaging.h>
#include <linux/rwlock.h>

#ifdef CONFIG_PFTRACE
#include <linux/tracepoint-defs.h>

DECLARE_TRACEPOINT(coala_rmqueue);
void do_trace_coala_rmqueue(unsigned long cycles);

DECLARE_TRACEPOINT(coala_ptescan);
void do_trace_coala_ptescan(unsigned long cycles);

DECLARE_TRACEPOINT(coala_pmdscan);
void do_trace_coala_pmdscan(unsigned long cycles);
#endif /* CONFIG_PFTRACE */

/* required forward declarations from mm/page_alloc.c */
static inline void del_page_from_free_list(struct page *page, struct zone *zone,
					   unsigned int order);
static inline void add_to_free_list_tail(struct page *page, struct zone *zone,
				    unsigned int order, int migratetype);
static inline void add_to_free_list(struct page *page, struct zone *zone,
				    unsigned int order, int migratetype);
static inline void zone_statistics(struct zone *preferred_zone, struct zone *z,
				   long nr_account);
static void prep_new_page(struct page *page, unsigned int order, gfp_t gfp_flags,
							unsigned int alloc_flags);
static inline bool pcp_allowed_order(unsigned int order);

static inline void __free_one_page(struct page *page,
		unsigned long pfn,
		struct zone *zone, unsigned int order,
		int migratetype, fpi_t fpi_flags);

static inline void expand_tail(struct zone *zone, struct page *page,
	int low, int high, int migratetype)
{
	unsigned long size = 1 << high;

	while (high > low) {
		high--;
		size >>= 1;
		add_to_free_list_tail(&page[size], zone, high, migratetype);
		set_buddy_order(&page[size], high);
	}
}

/*
 * helper function to split block from buddy free lists (used to
 * extract a target page from a larger block) and allocate the target page
 */
static inline void recursive_split_free_area(struct zone *zone,
		struct page *page, struct page *targetpage, int targetorder) {
	int order;
	unsigned long order_nrpages, targetpfn;

	/* allocate the target page (remove it from the buddy list) */
	order = buddy_order(page);
	order_nrpages = 1UL << order;
	del_page_from_free_list(page, zone, order);

	targetpfn = page_to_pfn(targetpage);

	while (page != targetpage) {
		order--;
		order_nrpages >>= 1;

		/*
		 * add the block not including the target page back and
		 * repeat the loop with the other block
		 */
		if (targetpfn >= page_to_pfn(page + order_nrpages)) {
			add_to_free_list_tail(page, zone, order,
					get_pageblock_migratetype(page));
			set_buddy_order(page, order);
			page += order_nrpages;
		} else  {
			add_to_free_list_tail(page + order_nrpages, zone, order,
					get_pageblock_migratetype(page + order_nrpages));
			set_buddy_order(page + order_nrpages, order);
		}
	}

	/* found the target page, expand the rest of the block */
	expand_tail(zone, targetpage, targetorder, order,
			get_pageblock_migratetype(targetpage));
}

/* VA->PMD walker */
static inline pmd_t *walk_to_pmd(struct mm_struct *mm, unsigned long addr) {
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_offset(pgd, addr);
	if (!p4d || p4d_none(*p4d))
		return NULL;
	pud = pud_offset(p4d, addr);
	if (!pud || pud_none(*pud) || pud_huge(*pud))
		return NULL;
	pmd = pmd_offset(pud, addr);
	if (!pmd || pmd_none(*pmd))
		return NULL;

	return pmd;
}

static inline void hpmd_order_topup(struct zone *zone, int migratetype) {
	int order = HPAGE_PMD_ORDER + 1;
	unsigned long order_nrpages;
	struct free_area *area;
	struct page *page = NULL;

	area = &(zone->free_area[order]);
	while (area->nr_free <= 2 * CONT_PMDS && order < MAX_ORDER) {
		area = &(zone->free_area[++order]);
	}

	if (order == MAX_ORDER) {
		return;
	}

	page = get_page_from_free_area(area, migratetype);
	if (!page) {
		//wakeup_kcompactd(NODE_DATA(0), HPAGE_PMD_ORDER, ZONE_NORMAL);
		return;
	}

	del_page_from_free_list(page, zone, order);
	order_nrpages = 1UL << order;

	while (order > HPAGE_PMD_ORDER) {
		order--;
		order_nrpages >>= 1;
		add_to_free_list(page + order_nrpages, zone, order,
				get_pageblock_migratetype(page + order_nrpages));
		set_buddy_order(page + order_nrpages, order);
	}

	add_to_free_list(page, zone, HPAGE_PMD_ORDER, get_pageblock_migratetype(page));
	set_buddy_order(page, order);

	return;
}

/* split a block and allocate a contpte page */
static inline struct page *get_contpte_page(struct zone *zone,
		int migratetype, unsigned long offset) {
	int order;
	unsigned long order_nrpages, contpte_order;
	struct free_area *area;
	struct page *target, *page = NULL;

	contpte_order = CONT_PTE_SHIFT - PAGE_SHIFT;

	for (order = contpte_order; order < MAX_ORDER; order++) {
		area = &(zone->free_area[order]);
		page = get_page_from_free_area(area, migratetype);
		if (!page) {
			continue;
		}

		del_page_from_free_list(page, zone, order);
		break;
	}

	if (!page) {
		return NULL;
	}

	/* If we got a bigger block, add the remainder back to the free lists */
	expand_tail(zone, page, contpte_order, order, get_pageblock_migratetype(page));

	target = page + offset;

	order = contpte_order;
	order_nrpages = 1UL << order;
	while (order) {
		order--;
		order_nrpages >>= 1;

		/*
		 * add the block not including the target page back and
		 * repeat the loop with the other block
		 */
		if (target>= page + order_nrpages) {
			add_to_free_list_tail(page, zone, order,
					get_pageblock_migratetype(page));
			set_buddy_order(page, order);
			page += order_nrpages;
		} else  {
			add_to_free_list_tail(page + order_nrpages, zone, order,
					get_pageblock_migratetype(page + order_nrpages));
			set_buddy_order(page + order_nrpages, order);
		}
	}
	return target;
}

static inline struct page *contpte_placement(struct zone *zone,
		int migratetype, struct coalareq *req, bool *unpopulated) {
	int i;
	unsigned long addr, voff;
	unsigned long poff, pfn, anchorpfn = 0;
	struct page *targetpage;
	pmd_t *pmd;
	pte_t *pte;

#ifdef CONFIG_PFTRACE
	unsigned long cycles = get_cycles();
#endif /* CONFIG_PFTRACE */

	/* start of the 64K range */
	addr = ALIGN_DOWN(req->vaddr, CONT_PTE_SIZE);
	for (i = 0; i < CONT_PTES; i++, addr += PAGE_SIZE) {
		/* skip the faulting addr */
		if (addr == (req->vaddr & PAGE_MASK)) {
			continue;
		}

		/* get the pte and skip the checks if it's not populated */
		pmd = walk_to_pmd(req->mm, addr);
		if (!pmd || pmd_none(*pmd)) {
			continue;
		}

		pte = pte_offset_kernel(pmd, addr);
		if (!pte || pte_none(*pte) || !pte_present(*pte)) {
			continue;
		}

		/* check for alignment */
		voff = (addr >> PAGE_SHIFT) & (CONT_PTES - 1);
		pfn = pte_pfn(*pte);
		poff = pfn & (CONT_PTES - 1);
		if (voff != poff) {
			continue;
		}

		/* no other ptes encountered thus far, set the anchor */
		if (!anchorpfn) {
			anchorpfn = ALIGN_DOWN(pfn, CONT_PTES);
			break;
		}
	}

	voff = (req->vaddr >> PAGE_SHIFT) & (CONT_PTES - 1);
	/* unpopulated range, get a page from a >=order-4 block */
	if (!anchorpfn) {
		targetpage = get_contpte_page(zone, migratetype, voff);
		if (targetpage) {
			if (unpopulated) {
				*unpopulated = true;
			}

#ifdef CONFIG_PFTRACE
			if (tracepoint_enabled(coala_ptescan)) {
				do_trace_coala_ptescan(get_cycles() - cycles);
			}
#endif /* CONFIG_PFTRACE */
			return targetpage;
		}

		if (unpopulated) {
			*unpopulated = false;
		}

#ifdef CONFIG_PFTRACE
		if (tracepoint_enabled(coala_ptescan)) {
			do_trace_coala_ptescan(get_cycles() - cycles);
		}
#endif /* CONFIG_PFTRACE */

		return NULL;
	}

	/* find the targetpage based on the range pfn */
	if (unpopulated) {
		*unpopulated = false;
	}

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(coala_ptescan)) {
		do_trace_coala_ptescan(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */

	return pfn_to_page(anchorpfn + voff);
}

static inline struct page *pagecache_placement(struct zone *zone,
		int migratetype, struct coalareq *req, bool *unpopulated) {
	struct address_space *mapping = req->mapping;
	unsigned long target_index = req->vaddr;
	unsigned long index, pfn, offset, anchor, anchorpfn = 0;
	struct folio *folio;
	struct page *targetpage;
	XA_STATE(xas, &mapping->i_pages, 0);

	anchor = ALIGN_DOWN(target_index, CONT_PTES);
	xas_set(&xas, anchor);
	for (index = anchor; index < anchor + CONT_PTES; ++index) {
		if (index == target_index) {
			continue;
		}

		folio = xas_next(&xas);
		if (xas_retry(&xas, folio) || !folio || xa_is_value(folio)) {
			continue;
		}

		pfn = page_to_pfn(folio_page(folio, 0));
		offset = pfn & (CONT_PTES - 1);


#if 0
		if (offset != (index & (CONT_PTES - 1))) {
			continue;
		}
#endif

		if (!anchorpfn) {
			anchorpfn = pfn & ~(CONT_PTES - 1);
			break;
		}
	}

	offset = target_index & (CONT_PTES - 1);
	if (!anchorpfn) {
		targetpage = get_contpte_page(zone, migratetype, offset);
		if (targetpage) {
			if (unpopulated) {
				*unpopulated = true;
			}
			return targetpage;
		}
		return NULL;
	}

	if (unpopulated) {
		*unpopulated = false;
	}

	return pfn_to_page(anchorpfn + offset);
}

static inline struct page *get_contpmd_page(struct zone *zone,
		int migratetype, unsigned long offset) {
	int order;
	unsigned long order_nrpages;
	struct free_area *area;
	struct page *target, *page = NULL;

	order = CONT_PMD_SHIFT - PAGE_SHIFT;
	order_nrpages = 1UL << order;

	area = &(zone->free_area[order]);
	page = get_page_from_free_area(area, migratetype);
	if (!page) {
		return NULL;
	}

	del_page_from_free_list(page, zone, order);

	target = page + (offset << (PMD_SHIFT - PAGE_SHIFT));

	while (order > HPAGE_PMD_ORDER) {
		order--;
		order_nrpages >>= 1;

		/*
		 * add the block not including the target page back and
		 * repeat the loop with the other block
		 */
		if (target >= page + order_nrpages) {
			add_to_free_list_tail(page, zone, order,
					get_pageblock_migratetype(page));
			set_buddy_order(page, order);
			page += order_nrpages;
		} else  {
			add_to_free_list_tail(page + order_nrpages, zone, order,
					get_pageblock_migratetype(page + order_nrpages));
			set_buddy_order(page + order_nrpages, order);
		}
	}

	area = &(zone->free_area[HPAGE_PMD_ORDER]);
	if (area->nr_free <= 2 * CONT_PMDS) {
		hpmd_order_topup(zone, migratetype);
	}
	
#if 0
	while (area->nr_free <= 4 * CONT_PMDS) {
		if (!hpmd_order_topup(zone, migratetype)) {
			break;
		}
		area = &(zone->free_area[HPAGE_PMD_ORDER]);
	}
#endif

	return target;
}

static inline struct page *contpmd_placement(struct zone *zone,
		int migratetype, struct coalareq *req, bool *unpopulated) {
	int i;
	unsigned long addr, voff;
	unsigned long poff, pfn, anchorpfn = 0;
	struct page *targetpage;
	pmd_t *pmd;
#ifdef CONFIG_PFTRACE
	unsigned long cycles = get_cycles();
#endif /* CONFIG_PFTRACE */

	/* start of the 32M range */
	addr = ALIGN_DOWN(req->vaddr, CONT_PMD_SIZE);
	for (i = 0; i < CONT_PMDS; i++, addr += PMD_SIZE) {
		/* skip the faulting addr */
		if (addr == (req->vaddr & PMD_MASK)) {
			continue;
		}

		/* get the pte and skip the checks if it's not populated */
		pmd = walk_to_pmd(req->mm, addr);
		if (!pmd || pmd_none(*pmd)) {
			continue;
		}

		if (pmd_present(*pmd) && !pmd_trans_huge(*pmd)) {
			continue;
		}

		/* check for alignment */
		voff = (addr >> PMD_SHIFT) & (CONT_PMDS - 1);
		pfn = pmd_pfn(*pmd);
		poff = (pfn >> (PMD_SHIFT - PAGE_SHIFT)) & (CONT_PMDS - 1);
		if (voff != poff) {
			continue;
		}

		/* no other ptes encountered thus far, set the anchor */
		if (!anchorpfn) {
			anchorpfn = ALIGN_DOWN(pfn, CONT_PMDS << (PMD_SHIFT - PAGE_SHIFT));
			break;
		}
	}

	voff = (req->vaddr >> PMD_SHIFT) & (CONT_PMDS - 1);
	/* unpopulated range, get a 2M block from a 32M contmap block */
	if (!anchorpfn) {
		targetpage = get_contpmd_page(zone, migratetype, voff);
		if (targetpage) {
			if (unpopulated) {
				*unpopulated = true;
			}
#ifdef CONFIG_PFTRACE
			if (tracepoint_enabled(coala_pmdscan)) {
				do_trace_coala_pmdscan(get_cycles() - cycles);
			}
#endif /* CONFIG_PFTRACE */
			return targetpage;
		}

		if (unpopulated) {
			*unpopulated = false;
		}

#ifdef CONFIG_PFTRACE
		if (tracepoint_enabled(coala_pmdscan)) {
			do_trace_coala_pmdscan(get_cycles() - cycles);
		}
#endif /* CONFIG_PFTRACE */

		return NULL;
	}

	/* find the targetpage based on the range pfn */
	if (unpopulated) {
		*unpopulated = false;
	}

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(coala_pmdscan)) {
		do_trace_coala_pmdscan(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */

	return pfn_to_page(anchorpfn + (voff << (PMD_SHIFT - PAGE_SHIFT)));
}

/* core coalapaging allocation routine */
static inline struct page *coala_rmqueue(struct zone *preferred_zone,
			struct zone *zone, unsigned int order, gfp_t gfp_flags,
			unsigned int alloc_flags, int migratetype,
			struct coalareq *req) {
	struct free_area *area;
	struct page *page, *targetpage, *buddy;
	struct per_cpu_pages *pcp;
	unsigned long flags, pfn, buddy_pfn, order_nrpages, targetpfn;
	unsigned int curr_order, __order;
	bool unpopulated = false;
#ifdef CONFIG_PFTRACE
	unsigned long cycles = get_cycles();
#endif /* CONFIG_PFTRACE */

	spin_lock_irqsave(&zone->lock, flags);

	if (!order) {
		if (req->mapping) {
			targetpage = pagecache_placement(zone, migratetype, req, &unpopulated);
		} else {
			targetpage = contpte_placement(zone, migratetype, req, &unpopulated);
		}
	} else {
		targetpage = contpmd_placement(zone, migratetype, req, &unpopulated);
	}

	if (unpopulated) {
		BUG_ON(!targetpage);
		BUG_ON(!pfn_valid(page_to_pfn(targetpage)));
		goto success;
	}

	if (!targetpage) {
		goto fail;
	}

	targetpfn = page_to_pfn(targetpage);
	if (!pfn_valid(targetpfn)) {
		goto fail;
	}

	/* if the pfn is out of the requested zone (and NUMA node), fail */
	if (bad_range(zone, targetpage) ||
		zone_to_nid(page_zone(targetpage)) != zone_to_nid(zone)) {
		goto fail;
	}

	/* page is guard, fail */
	if (page_is_guard(targetpage)) {
		goto fail; 
	}

	/* page is slab, fail */
	if (PageSlab(targetpage)) {
		goto fail; 
	}

	/* page is occupied, fail */
	if (page_mapcount(targetpage) > 0 || page_count(targetpage) > 0) {
		goto fail;
	}

	/* page is part of the PCP free lists */
	if (pcp_allowed_order(order) &&
		get_pcppage_migratetype(targetpage) >= MIGRATE_PCPTYPES &&
		PageCaPcpFree(targetpage)) { /* fast path check */

		/* lock the page */
		while (TestSetPagePcplocked(targetpage)) ;

		/* get the pcp struct from the page mapping */
		pcp = (struct per_cpu_pages *)targetpage->mapping;
		if (!PageCaPcpFree(targetpage) || !pcp ||
			order != buddy_order(targetpage)) {
			ClearPagePcplocked(targetpage);
			goto fail;
		}

		/* remove the page from the pcp lists */
		__ClearPageCaPcpFree(targetpage);
		list_del(&targetpage->lru);
		targetpage->mapping = NULL;
		pcp->count -= 1 << order;
		ClearPagePcplocked(targetpage);

		goto success;
	}

	/* target page is free and marked as Buddy */
	if (PageBuddy(targetpage)) {
		/* current order of the block */
		__order = buddy_order(targetpage);

		/* it has smaller than requested size, fail */
		if (__order < order) {
			goto fail;
		}

		area = &zone->free_area[__order];

		/* allocate the target page */
		del_page_from_free_list(targetpage, zone, __order);

		/*  if the block was larger split it */
		if (__order > order) {
			expand_tail(zone, targetpage, order, __order, migratetype);
		}
	}
	/* the page could be free but part of a larger block */
	else {
		/* calculates buddies and checks if they're free */
		pfn = targetpfn;
		page = targetpage;
		for (curr_order = order + 1; curr_order < MAX_ORDER; curr_order++) {
			buddy_pfn = pfn & ~(1UL << (curr_order - 1));
			buddy = pfn_to_page(buddy_pfn);

			/* buddy is slab or occupied, fail */
			if (PageSlab(buddy) || page_mapcount(buddy) > 0 ||
					page_count(buddy) > 0) {
				goto fail;
			}

			/* exceed zone, fail */
			if (page_zone_id(buddy) != page_zone_id(targetpage)) {
				goto fail;
			}

			/*  found a buddy superblock */
			if (PageBuddy(buddy) && buddy_order(buddy) >= curr_order) {
				curr_order = buddy_order(buddy);
				break;
			}

			pfn = buddy_pfn;
			page = buddy;
		}

		/* guard, fail */
		if (page_is_guard(buddy)) {
			goto fail;
		}

		/* nothing found */
		if (curr_order >= MAX_ORDER) {
			goto fail;
		}

		/* split the buddy block and allocate the target page */
		recursive_split_free_area(zone, buddy, targetpage, order);
	}

success:
	order_nrpages = 1UL << order;
	__mod_zone_freepage_state(zone, -order_nrpages,
			get_pcppage_migratetype(targetpage));
	spin_unlock_irqrestore(&zone->lock, flags);
	__count_zid_vm_events(PGALLOC, page_zonenum(targetpage), order_nrpages);
	zone_statistics(preferred_zone, zone, 1);

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(coala_rmqueue)) {
		do_trace_coala_rmqueue(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */

#if 0
	if (coala_hints_enabled(req->mm) && order) {
		pr_debug("faulted %u-order hint",
				order ? COALA_HINT_32M : COALA_HINT_64K);
		coala_clear_khugepaged_mark(req->mm, req->vaddr,
				order ? COALA_HINT_32M : COALA_HINT_64K);
		SetPageLeshy(targetpage);
	}
#endif

	return targetpage;

fail:
	spin_unlock_irqrestore(&zone->lock, flags);

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(coala_rmqueue)) {
		do_trace_coala_rmqueue(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */

	return NULL;
}

#endif /* __LINUX_COALAPAGING_ALLOC_H */
