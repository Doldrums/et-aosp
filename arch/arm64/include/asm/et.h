#ifndef __ASM_ET_H
#define __ASM_ET_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_PFTRACE
#include <linux/tracepoint-defs.h>

/* TLB flush latency tracepoint */
DECLARE_TRACEPOINT(etflush);
void do_trace_etflush(unsigned long cycles);

/* PTE /PMD set latency tracepoint */
DECLARE_TRACEPOINT(etset);
void do_trace_etset(unsigned long cycles);
#endif /* CONFIG_PFTRACE */

/* ET module params */
extern bool et_global;
extern bool et_batched;
extern bool et_flush_full;
extern bool et_cacheline_opt;
extern bool et_validate;

/* Number of contiguous ranges */
#define NR_ENTRIES (CONT_PTES > CONT_PMDS ? CONT_PTES : CONT_PMDS)

/* Cacheline size for the cacheline optimization */
#define ET_CACHELINE_SZ 0x8

/* TLB level hints for TTL-capable CPUs */
#define ET_L2_PT 0x2 /* PMD */
#define ET_L3_PT 0x3 /* PTE */

/*
 * For the purposes of ET creation, two PTEs are the same if they
 *	- have the same PFN and
 *  - have the same prot (minus AF / DIRTY / CONT) (FIXME)
 *	(if PFN == OLDPFN, XORing the PTEs gives the prot diff)
 */
#define ET_PROT_IGNORE_MASK ~(PTE_AF | PTE_DIRTY | PTE_CONT)
#define et_ptes_same(pte_a, pte_b) !(((pte_a) ^ (pte_b)) & ET_PROT_IGNORE_MASK)

/* Valid exec-only user mappings */
#define ET_VALID_USER_XO (PTE_VALID | PTE_PXN)
/* Valid RO / RW user mappings */
#define ET_VALID_USER (ET_VALID_USER_XO | PTE_USER | PTE_UXN)

/* Valid mask for PTEs (== ET_VALID_USER) */
#define ET_PTE_PROT_MASK ET_VALID_USER
/* For PMDs we also need to include the TABLE_BIT to exclude table entries */
#define ET_PMD_PROT_MASK (ET_PTE_PROT_MASK | PTE_TABLE_BIT)

/* Valid prot helper */
#define et_prot_check(pte, pmask, extra)				\
({														\
	pteval_t prot = pte_val(pte) & ((pmask) | (extra));	\
	((prot == (ET_VALID_USER_XO | (extra))) ||			\
	 (prot == (ET_VALID_USER | (extra))));				\
})
/* Filter for valid user PTEs (candidates for ET creation) */
#define et_prot_valid(pte, pmask) et_prot_check(pte, pmask, 0)
/* Filter for ET PTEs (valid and have PTE_CONT set) */
#define et_is_etpte(pte, pmask) et_prot_check(pte, pmask, PTE_CONT)

/* Range helpers */
#define et_anchor_frame(frame, nr_entries, shift) \
	ALIGN_DOWN(frame, (nr_entries) << ((shift) - PAGE_SHIFT))
#define et_range_offset(frame, nr_entries, shift) \
	(((frame) >> ((shift) - PAGE_SHIFT)) & ((nr_entries) - 1))
#define et_frame_at(anchor, offset, shift) \
	((anchor) + ((offset) << ((shift) - PAGE_SHIFT)))

/* Check if the PFN is range-aligned wrt anchor */
#define et_pfn_anchored(pfn, apfn, shift, offset) \
	((pfn) == (apfn) + ((offset << ((shift) - PAGE_SHIFT))))
/* Check if the PFN offset is range-aligned */
#define et_pfn_aligned(pfn, nr_entries, shift, offset) \
	(et_range_offset(pfn, nr_entries, shift) == (offset))

#ifdef CONFIG_DEBUG_ET
#define ET_BUG_ON(cond, ctx, offset) ({										\
		if (unlikely(cond)) {												\
			et_dump_pte(ctx, offset, "ET_BUG_ON(" __stringify(cond)")");	\
			WARN_ON(1);														\
		}																	\
})
#else /* !CONFIG_DEBUG_ET */
#define ET_BUG_ON(cond, ctx, offset) ({	})
#endif /* !CONFIG_DEBUG_ET */

/* 
 * FIXME: We only enable et for CAPaging-enabled mm's, VM_CONTIG VMAs not
 * supported atm.
 */
static inline bool is_et_enabled(struct mm_struct *mm) {
	return mm && (et_global || mm->et_enabled);
}

/* Selector for the et_defs */
enum et_pt_lvl {
	ET_PTE = 0,
	ET_PMD,
	NR_ET_PT_LVLS,
};

/* Defs / consts for PTEs and PMDs */
struct et_defs_struct {
	unsigned tlb_lvl_hint;		/* ET_L[23]_PT */
	unsigned nr_entries;		/* CONT_PTES / CONT_PMDS */
	unsigned long prot_mask;	/* ET_P(TE|MD)_PROT_MASK */
	unsigned pshift;			/* PAGE_SHIFT / PMD_SHIFT */
};

static const struct et_defs_struct et_defs[NR_ET_PT_LVLS] = {
	{ .tlb_lvl_hint = ET_L3_PT,
	  .nr_entries = CONT_PTES,
	  .prot_mask = ET_PTE_PROT_MASK,
	  .pshift = PAGE_SHIFT,
	},
	{ .tlb_lvl_hint = ET_L2_PT,
	  .nr_entries = CONT_PMDS,
	  .prot_mask = ET_PMD_PROT_MASK,
	  .pshift = PMD_SHIFT,
	},
};


/* ET context struct (one per et_set_entry_at()) */
struct et_ctx {
	struct mm_struct *mm;

	unsigned long addr;			/* VA to be updated */
	pte_t *ptep;				/* PTE for the VA to be updated */
	pte_t pte;					/* New PTE value */

	enum et_pt_lvl level;		/* ET_PTE or ET_PMD */

	unsigned long anchor_vpn;	/* Range-aligned (anchor) VA */
	pte_t *anchor_ptep;			/* Range-aligend (anchor) PTE */
	pteval_t anchor_pfn;		/* Range-aligned (anchor) PFN */
	int offset;					/* Offset within the range */

	pte_t entries[NR_ENTRIES];	/* Saved entries during range creation */

	struct et_defs_struct defs;	/* Per-PT level constants */
};

static inline void et_dump_pte(struct et_ctx *ctx, int offset,
		const char *msg) {
	pr_debug("%s: PTE@%lx(=%llx), offset: %d, level: %d, "
			 "anchor PTE@%lx(anchor PFN=%llx) for VPN@%lx (anchor VPN@%lx)",
			 msg, (unsigned long)&ctx->anchor_ptep[offset],
			 pte_val(ctx->entries[offset]), offset, ctx->level,
			 (unsigned long)ctx->anchor_ptep, ctx->anchor_pfn,
			 et_frame_at(ctx->anchor_vpn, offset, ctx->defs.pshift),
			 ctx->anchor_vpn);
}

/* Helper to check if a group of PTEs has range-compatible offsets + flags */
static inline bool et_check_chunk(struct et_ctx *ctx, int start, int end) {
	int i;
	pteval_t pfn;

	for (i = start; i < end; i++) {
		ctx->entries[i] = READ_ONCE(ctx->anchor_ptep[i]);
		pfn = pte_pfn(ctx->entries[i]);

		if (!et_prot_valid(ctx->entries[i], ctx->defs.prot_mask) ||
			!et_pfn_anchored(pfn, ctx->anchor_pfn, ctx->defs.pshift, i)) {
			return false;
		}
		ET_BUG_ON(!et_pfn_aligned(pfn, ctx->defs.nr_entries,
					ctx->defs.pshift, i), ctx, i);
	}

	return true;
}

#ifdef CONFIG_DEBUG_ET
static inline void et_validate_entries(struct et_ctx *ctx, bool is_et,
		bool use_cached, const char *msg) {
	if (unlikely(et_validate)) {
		int i;
		pteval_t pfn;

		pr_debug("validate: %s", msg);

		for (i = 0; i < ctx->defs.nr_entries; i++) {
			if (!use_cached) {
				ctx->entries[i] = READ_ONCE(ctx->anchor_ptep[i]);
			}
			pfn = pte_pfn(ctx->entries[i]);

			if (is_et) {
				ET_BUG_ON(!et_is_etpte(ctx->entries[i],
							ctx->defs.prot_mask), ctx, i);
				ET_BUG_ON(!et_pfn_aligned(pfn, ctx->defs.nr_entries,
							ctx->defs.pshift, i), ctx, i);
				ET_BUG_ON(!et_pfn_anchored(pfn, ctx->anchor_pfn,
							ctx->defs.pshift, i), ctx, i);
			} else {
				ET_BUG_ON(pte_val(ctx->entries[i]) & PTE_CONT, ctx, i);
			}
		}
	}
}
#else /* !CONFIG_DEBUG_ET */
static inline void et_validate_entries(struct et_ctx *ctx, bool is_et,
		bool use_cached, const char *msg) { 
}
#endif /* !CONFIG_DEBUG_ET */

/* Check if the PTEs within a range are contiguous and with compatible flags */
static inline bool et_candidate(struct et_ctx *ctx) {
	int start, end;

	if (unlikely(!et_cacheline_opt)) {
		start = 0;
		end = ctx->defs.nr_entries;
	} else {
		start = ALIGN_DOWN(ctx->offset, ET_CACHELINE_SZ);
		end = ALIGN(ctx->offset, ET_CACHELINE_SZ);
		if (start == end) {
			end += ET_CACHELINE_SZ;
		}
	}

	/* check the ptep cacheline first (probably useless optimization) */
	return et_check_chunk(ctx, start, ctx->offset) && 
		et_check_chunk(ctx, ctx->offset + 1, end) && 
		et_check_chunk(ctx, end, ctx->defs.nr_entries) &&
		et_check_chunk(ctx, 0, start);
}

/*
 * Copied from tlbflush.h, specialized for ET.
 *
 * We use the valel1 tlbi variants and also provide the level of the
 * translation, in case the CPU supports TTL hints.
 *
 * For CPUs which support range invalidations we can flush ranges with a
 * single tlbi.
 */
static inline void et_flush_tlb_range(struct et_ctx *ctx) {
	int i;
	unsigned long asid, addr, stride, scale, num;

#ifdef CONFIG_PFTRACE
	unsigned long cycles = get_cycles();
#endif /* CONFIG_PFTRACE */

	dsb(ishst);

	stride = 1UL << ctx->defs.pshift;
	asid = ASID(ctx->mm);
	addr = ctx->anchor_vpn << PAGE_SHIFT;

	if (likely(!system_supports_tlb_range())) {
		pr_debug("Flushing %d entries for range@0x%lx, asid=0x%lx, stride=0x%lx",
				ctx->defs.nr_entries, ctx->anchor_vpn, asid, stride);

		for (i = 0; i < ctx->defs.nr_entries; i++, addr += stride) {
			unsigned long tlbi_addr = __TLBI_VADDR(addr, asid);

			pr_debug("Flushing 0x%lx (0x%lx), level=%d", addr, tlbi_addr,
					ctx->defs.tlb_lvl_hint);

			__tlbi_level(vale1is, tlbi_addr, ctx->defs.tlb_lvl_hint);
			__tlbi_user_level(vale1is, tlbi_addr, ctx->defs.tlb_lvl_hint);
		}
		dsb(ish);

#ifdef CONFIG_PFTRACE
		if (tracepoint_enabled(etflush)) {
			do_trace_etflush(get_cycles() - cycles);
		}
#endif /* CONFIG_PFTRACE */

		return;
	}

	/*
	 * when CONT_PTES = 16 or 32, we need a scale of 0
	 * when CONT_PTES = 128 we need a scale of 1
	 * for CONT_PMDS (16 or 32) we need a scale of 2
	 */
	scale = 2 * (stride > PAGE_SIZE) + (ctx->defs.nr_entries > 64);
	num = (ctx->defs.nr_entries << (ctx->defs.pshift - PAGE_SHIFT))  /
		(1 << (5 * scale + 1));

	addr = __TLBI_VADDR_RANGE(addr, asid, scale, num, ctx->defs.tlb_lvl_hint);
	__tlbi(rvale1is, addr);
	__tlbi_user(rvale1is, addr);
	dsb(ish);

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(etflush)) {
		do_trace_etflush(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */
}

/* Helper / context struct for batch flushes */
struct et_batch {
	unsigned long batch_vpn;
	int cur;
	pte_t entries[NR_ENTRIES];
};

/* the 'heart' of ET */
static inline pte_t et_set_entry_at(struct et_ctx *ctx) {
	int i;
	pte_t old_pte;

#ifdef CONFIG_PFTRACE
	unsigned long cycles = get_cycles();
#endif /* CONFIG_PFTRACE */

	/* Set the PTE / PMD constants */
	ctx->defs = et_defs[ctx->level];

#ifdef CONFIG_DEBUG_ET
	if (unlikely(et_validate)) {
		/* Zero the PTEs array, just in case ... */
		memset(ctx->entries, 0, sizeof(ctx->entries));
	}
#endif /* CONFIG_DEBUG_ET */

	/* Get the anchor PTEP, VPN */
	ctx->anchor_ptep = PTR_ALIGN_DOWN(ctx->ptep,
			ctx->defs.nr_entries * sizeof(ctx->ptep));
	ctx->anchor_vpn = et_anchor_frame(ctx->addr >> PAGE_SHIFT,
			ctx->defs.nr_entries, ctx->defs.pshift);

	/* Range offset for the updated PTE */
	ctx->offset = ctx->ptep - ctx->anchor_ptep;

	/* Get the old PTE value and the old anchor PFN */
	old_pte = READ_ONCE(*ctx->ptep);
	ctx->entries[ctx->offset] = old_pte;
	ctx->anchor_pfn = et_anchor_frame(pte_pfn(old_pte),
			ctx->defs.nr_entries, ctx->defs.pshift);

	/* This should never happen, PTEP offset should match the VA offset */
	ET_BUG_ON(ctx->offset != et_range_offset(ctx->addr >> PAGE_SHIFT,
				ctx->defs.nr_entries, ctx->defs.pshift), ctx, ctx->offset);

	/* 
	 * PTE update preseved PFN and PROT, we just need to update the PTE,
	 * regardless of its PTE_CONT status
	 */
	if (et_ptes_same(pte_val(old_pte), pte_val(ctx->pte))) {
		unsigned long is_range = et_is_etpte(old_pte, ctx->defs.prot_mask);

		et_dump_pte(ctx, ctx->offset, "PTE update preserved PFN and PROT");
		et_validate_entries(ctx, is_range, false, "et_ptes_same enter");

		/* set the new pte and preserve the range status */
		set_pte(ctx->ptep, __pte(pte_val(ctx->pte) | (is_range * PTE_CONT)));

		et_validate_entries(ctx, is_range, false, "et_ptes_same exit");

#ifdef CONFIG_PFTRACE
		if (tracepoint_enabled(etset)) {
			do_trace_etset(get_cycles() - cycles);
		}
#endif /* CONFIG_PFTRACE */

		return __pte(pte_val(old_pte) & ~PTE_CONT);
	}

	/* Check if the PTE is valid and part of an ET */
	if (!et_is_etpte(old_pte, ctx->defs.prot_mask)) {
		/* Make sure that for non-ET PTEs PTE_CONT is cleared */
		ET_BUG_ON(pte_val(old_pte) & PTE_CONT, ctx, ctx->offset);
		et_validate_entries(ctx, false, false, "before range check");

		/* New pte */
		ctx->entries[ctx->offset] = ctx->pte;
		ctx->anchor_pfn = et_anchor_frame(pte_pfn(ctx->pte),
				ctx->defs.nr_entries, ctx->defs.pshift);

		/*
		 * PTE doesn't belong to an ET and any of the entries:
		 * 	- are invalid or
		 * 	- Anchor PFN-misaligned or
		 * 	- PFN range offset-misaligned
		 */
		if (!et_prot_valid(ctx->pte, ctx->defs.prot_mask) || 
			!et_pfn_aligned(pte_pfn(ctx->pte),
				ctx->defs.nr_entries, ctx->defs.pshift, ctx->offset) ||
			!et_candidate(ctx)) {
			et_validate_entries(ctx, false, false, "create skip before");

			set_pte(ctx->ptep, __pte(pte_val(ctx->pte) & ~PTE_CONT));

			et_validate_entries(ctx, false, false, "create skip after");

#ifdef CONFIG_PFTRACE
			if (tracepoint_enabled(etset)) {
				do_trace_etset(get_cycles() - cycles);
			}
#endif /* CONFIG_PFTRACE */

			return __pte(pte_val(old_pte) & ~PTE_CONT);
		}

		et_validate_entries(ctx, false, true, "before create set");
		et_dump_pte(ctx, ctx->offset, "New ET");

		/*
		 * FIXME: Maybe we can skip BBM since prot / translation doesn't
		 * change, but this could trigger an abort
		 */
		if (unlikely(et_flush_full)) {
			/* Zero entries to comply with BBM */
			for (i = 0; i < ctx->defs.nr_entries; i++) {
				set_pte(&ctx->anchor_ptep[i], __pte(0));
			}
			et_flush_tlb_range(ctx);
		}

		/* set PTE_CONT and update the entries */
		for (i = 0; i < ctx->defs.nr_entries; i++) {
			et_dump_pte(ctx, i, "Setting PTE");
			set_pte(&ctx->anchor_ptep[i], __pte(pte_val(ctx->entries[i]) | PTE_CONT));
		}

		et_validate_entries(ctx, true, false, "after create set");

		/*
		 * FIXME: can we skip the tlb flush here too and let the new cont
		 * entries populate the TLBs lazily?
		 */
		if (unlikely(et_flush_full)) {
			et_flush_tlb_range(ctx);
		}

#ifdef CONFIG_PFTRACE
		if (tracepoint_enabled(etset)) {
			do_trace_etset(get_cycles() - cycles);
		}
#endif /* CONFIG_PFTRACE */

		return __pte(pte_val(old_pte) & ~PTE_CONT);
	}

	/* PTE is valid and part of an ET, break the ET */
	et_dump_pte(ctx, ctx->offset, "Breaking ET");
	et_validate_entries(ctx, true, false, "before break zero");

	/* Zero the entries to comply with BBM */
	for (i = 0; i < ctx->defs.nr_entries; i++) {
		ctx->entries[i] = READ_ONCE(ctx->anchor_ptep[i]);
		set_pte(&ctx->anchor_ptep[i], __pte(0));
	}
	et_flush_tlb_range(ctx);

	/* Set the new PTE */
	ctx->entries[ctx->offset] = ctx->pte;

	/* Clear PTE_CONT and restore the PTEs */
	for (i = 0; i < ctx->defs.nr_entries; i++) {
		et_dump_pte(ctx, i, "Setting PTE");
		set_pte(&ctx->anchor_ptep[i],
				__pte(pte_val(ctx->entries[i]) & ~PTE_CONT));
		pr_debug("New PTE should be %llx, is %llx",
				pte_val(ctx->entries[i]) & ~PTE_CONT,
				pte_val(READ_ONCE(ctx->anchor_ptep[i])));
	}

	/* FIXME: Can we skip this? */
	if (unlikely(et_flush_full)) {
		et_flush_tlb_range(ctx);
	}

	et_validate_entries(ctx, false, false, "after break flush");

#ifdef CONFIG_PFTRACE
	if (tracepoint_enabled(etset)) {
		do_trace_etset(get_cycles() - cycles);
	}
#endif /* CONFIG_PFTRACE */

	/* FIXME: Should we try not to expose the contig bit outwards? */
	return __pte(pte_val(old_pte) & ~PTE_CONT);
}

#endif /* __ASSEMBLY__ */
#endif /* __ASM_ET_H */
