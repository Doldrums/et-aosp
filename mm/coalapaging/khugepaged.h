/* included in khugepaged.c */
#ifndef __LINUX_COALAPAGING_KHUGEPAGED_H
#define __LINUX_COALAPAGING_KHUGEPAGED_H

#define COALA_SCANONLY	((void *)0x1)

/* forward declaration from khugepaged.c */
static int khugepaged_scan_pmd(struct mm_struct *mm,
			       struct vm_area_struct *vma,
			       unsigned long address,
			       struct page **hpage);

/* mm_find_pmd() variation to return the raw pmd pointer */
static inline pmd_t *mm_find_pmdp(struct mm_struct *mm, unsigned long address) {
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		goto out;

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
out:
	return pmd;
}

static inline bool coala_khugepaged_skip_mm(struct mm_struct *mm) {
	/* Skip mm if khugepaged is disabled for coala-enabled mm's */
	if (mm->et_enabled && !coala_khugepaged) {
		return true;
	}

	if (!mm->et_enabled && coala_khugepaged) {
		return true;
	}

#if 0
	/* FIXME: For the moment, give exclusive priority to coala-enabled mm's */
	if (atomic64_read(&coala_hints_active) && !coala_hints_enabled(mm)) {
		return true;
	}
#endif

	return false;
}

static struct page *coala_khugepaged_alloc(struct mm_struct *mm,
		struct page **hpage, int node) {
	struct page *pages, *page;
	gfp_t gfp;
	int i;
	nodemask_t nmask;

	gfp = GFP_HIGHUSER_MOVABLE | __GFP_NOWARN | __GFP_ETRECLAIM | __GFP_NOMEMALLOC;
	gfp &= ~__GFP_RECLAIM;
	gfp |= __GFP_DIRECT_RECLAIM;

	*hpage = ERR_PTR(-ENOMEM);

	node = 0;
	nodes_clear(nmask);
	node_set(0, nmask);
	
	pages = __alloc_pages(gfp, MAX_ORDER - 1, node, &nmask);
	if (!pages) {
		pr_debug("alloc failed!");
		goto out;
	}

#if 0
	pr_crit("allocated page from node %d (requested node %d)",
			folio_nid(page_folio(pages)), node);
#endif

	page = pages;
	for (i = 0; i < CONT_PMDS; i++, page += HPAGE_PMD_NR) {
		page->mapping = NULL;
		set_page_private(page, 0);
		set_page_count(page, 1);
		prep_compound_page(page, HPAGE_PMD_ORDER);
		prep_transhuge_page(page);

		if (unlikely(mem_cgroup_charge(page_folio(page), mm, gfp))) {
			BUG();
		}
		count_memcg_page_event(page, THP_COLLAPSE_ALLOC);
	}

	*hpage = pages;
out:
	return pages;
}

static inline void coala_khugepaged_free(struct page *page) {
	int i;
	for (i = 0; i < CONT_PMDS; i++) {
		put_page(page + i);
	}
}

static void coala_khugepaged_collapse_contpmd(struct mm_struct *mm,
		unsigned long address, struct page **hpage, int node) {
	struct page *page, *new_page;
	struct folio *folio;
	struct vm_area_struct *vma;
	pmd_t *pmdp[CONT_PMDS], pmd[CONT_PMDS], _pmd;
	struct mmu_notifier_range range;
	spinlock_t *ptl[CONT_PMDS], *pte_ptl[CONT_PMDS];
	pte_t *pte[CONT_PMDS];
	int i, failed = 0;
	unsigned long _addr = address;
	bool pte_mapped[CONT_PMDS];
	pgtable_t pgtable[CONT_PMDS];

	mmap_read_unlock(mm);
	mmap_write_lock(mm);

	if (IS_ERR_OR_NULL(*hpage)) {
		pr_crit("wtf1 hpage 0x%lx", (unsigned long)*hpage);
		mmap_write_unlock(mm);
		return;
	}

	if (hugepage_vma_revalidate(mm, address, &vma)) {
		mmap_write_unlock(mm);
		pr_debug("vma revalidate fail");
		return;
	}

	anon_vma_lock_write(vma->anon_vma);

	/* FIXME: revalidate race */

	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, NULL, mm,
				_addr, _addr + HPAGE_PMD_SIZE * CONT_PMDS);
	mmu_notifier_invalidate_range_start(&range);

	for (i = 0, _addr = address; i < CONT_PMDS; i++, _addr += HPAGE_PMD_SIZE) {
		pmdp[i] = mm_find_pmdp(mm, _addr);
		ptl[i] = pmd_lock(mm, pmdp[i]);

		pmd[i] = READ_ONCE(*pmdp[i]);
	
		BUG_ON(pmd_none(pmd[i]));

		pte_mapped[i] = !pmd_trans_huge(pmd[i]);

		if (pte_mapped[i]) {
			pte[i] = pte_offset_map(pmdp[i], _addr);
			pte_ptl[i] = pte_lockptr(mm, pmdp[i]);
			spin_lock(pte_ptl[i]);
		}

		pmd[i] = pte_mapped[i] ? pmdp_collapse_flush(vma, _addr, pmdp[i]) :
			pmdp_huge_clear_flush(vma, _addr, pmdp[i]);

		BUG_ON(!pmd_none(*pmdp[i]));

		spin_unlock(ptl[i]);
	}

	pr_debug("finished collapse");

	mmu_notifier_invalidate_range_end(&range);

	pr_debug("notifier end");

	for (i = 0, _addr = address, new_page = *hpage; i < CONT_PMDS;
			i++, _addr += HPAGE_PMD_SIZE, new_page += HPAGE_PMD_NR) {
		if (pte_mapped[i]) {
			int retries = 2;
			LIST_HEAD(compound_pagelist);

			do {
				if (__collapse_huge_page_isolate(vma, _addr, pte[i],
							&compound_pagelist)) {
					break;
				}
				pr_warn("collapse failed retrying");
			} while (--retries);

			spin_unlock(pte_ptl[i]);

			if (unlikely(!retries)) {
				pte_unmap(pte[i]);
				BUG_ON(!pmd_none(*pmdp[i]));
				pmd_populate(mm, pmdp[i], pmd_pgtable(pmd[i]));
				failed++;
				pr_crit("isolate failed!");
				continue;
			}

			__collapse_huge_page_copy(pte[i], new_page, vma, _addr, pte_ptl[i],
					&compound_pagelist);

			pte_unmap(pte[i]);

			goto out_set;
		}

		if (!pmd_write(pmd[i])) {
			BUG();
		}

		page = vm_normal_page_pmd(vma, _addr, pmd[i]);
		if (unlikely(!page)) {
			BUG();
		}

		folio = page_folio(page);
		if (!folio_trylock(folio)) {
			BUG();
		}

		if (folio_isolate_lru(folio)) {
			BUG();
		}

		node_stat_add_folio(folio, NR_ISOLATED_ANON);
		copy_user_huge_page(new_page, page, _addr, vma, HPAGE_PMD_NR);
		page_remove_rmap(page, vma, true);
		folio_put(folio);
		node_stat_sub_folio(folio, NR_ISOLATED_ANON);
		folio_unlock(folio);
		folio_putback_lru(folio);

out_set:
		__SetPageUptodate(new_page);
		if (pte_mapped[i]) {
			pgtable[i] = pmd_pgtable(pmd[i]);
		}

		_pmd = mk_huge_pmd(new_page, vma->vm_page_prot);
		_pmd = maybe_pmd_mkwrite(pmd_mkdirty(_pmd), vma);

		ptl[i] = pmd_lock(mm, pmdp[i]);
		BUG_ON(!pmd_none(*pmdp[i]));
		page_add_new_anon_rmap(new_page, vma, _addr, true);
		lru_cache_add_inactive_or_unevictable(new_page, vma);
		if (pte_mapped[i]) {
			pgtable_trans_huge_deposit(mm, pmdp[i], pgtable[i]);
		}
		set_pmd_at(mm, _addr, pmdp[i], _pmd);
		update_mmu_cache_pmd(vma, _addr, pmdp[i]);
		spin_unlock(ptl[i]);

		pr_debug("coala increased collapsed!");
		khugepaged_pages_collapsed++;
	}

	anon_vma_unlock_write(vma->anon_vma);

	if (!failed) {
		coala_clear_khugepaged_mark(mm, address, COALA_HINT_32M);
		pr_debug("promoted order-13 hint!");
	} else {
		pr_warn("%d pmds failed for contpmd", failed);
	}
	*hpage = NULL;

	mmap_write_unlock(mm);
}

static int coala_khugepaged_scan_contpmd(struct mm_struct *mm, 
		struct vm_area_struct *vma, unsigned long address,
		struct page **hpage, bool *cont) {
	struct folio *folio;
	pmd_t *pmdp, pmd;
	spinlock_t *ptl;
	struct page *page;
	int ret = 0;
	int node, i, pmd_none = 0;
	unsigned long _addr = address, populate[CONT_PMDS];

	*cont = true;
	pmdp = mm_find_pmdp(mm, address);
	if (!pmdp) {
		return 0;
	}

	if (pmd_cont(*pmdp)) {
		coala_clear_khugepaged_mark(mm, address, COALA_HINT_32M);
		pr_debug("page already contpmd");
		return 0;
	}

	*cont = false;

	//memset(populate, 0, CONT_PMDS * sizeof(unsigned long));

	ptl = pmd_lock(mm, pmdp);
	for (i = 0, _addr = address; i < CONT_PMDS; i++, pmdp++, _addr += HPAGE_PMD_SIZE) {
		pmd = READ_ONCE(*pmdp);

#if 0
		if (pmd_none(pmd)) {
			pr_debug("pmd is none");
			pmd_none++;
			populate[i] = _addr;
			continue;
		}
#endif

		if (!pmd_trans_huge(pmd)) {
			if (!khugepaged_scan_pmd(mm, vma, _addr, COALA_SCANONLY)) {
				pr_debug("4K collapse failed for 0x%lx, present: %d, transhuge: %d, pmd: %llx",
						_addr, pmd_present(pmd), pmd_trans_huge(pmd), pmd_val(pmd));
				goto out_unlock;
			} else {
				continue;
			}
		}

		if (!pmd_write(pmd)) {
			pr_debug("pmd not writeable");
			goto out_unlock;
		}

		page = vm_normal_page_pmd(vma, _addr, pmd);
		if (!page) {
			pr_debug("vm_normal_page fail");
			goto out_unlock;
		}

		folio = page_folio(page);
		if (!folio_test_anon(folio) || folio_test_locked(folio) || !folio_test_lru(folio)) {
			pr_debug("folio not anon / locked / lru");
			goto out_unlock;
		}

#if 0
		node = folio_nid(folio);
		if (khugepaged_scan_abort(node)) {
			goto out_unlock;
		}
		khugepaged_node_load[node]++;
#endif
	}

#if 0
	if (pmd_none) {
		spin_unlock(ptl);
		if (pmd_none <= 2) {
			for (i = 0; i < CONT_PMDS; i++) {
				if (populate[i]) {
					pr_debug("populating 0x%lx", populate[i]);
					populate_vma_page_range(vma, populate[i],
							populate[i] + HPAGE_PMD_SIZE, NULL);
				}
			}
		}
		return 0;
	}
#endif

	ret = 2;

out_unlock:
	spin_unlock(ptl);
	if (ret) {
		//node = khugepaged_find_target_node();
		node = 0;
		if (!*hpage) {
			mmap_read_unlock(mm);
			if (!coala_khugepaged_alloc(mm, hpage, node)) {
				pr_debug("Failed alloc?");
				return 1;
			}
			mmap_read_lock(mm);
		}

		if (IS_ERR_OR_NULL(*hpage)) {
			pr_crit("wtf hpage 0x%lx", (unsigned long)*hpage);
			mmap_read_unlock(mm);
			return 1;
		}

		coala_khugepaged_collapse_contpmd(mm, address, hpage, node);
	}
	return ret;
}

static bool coala_khugepaged_scan_mm(struct mm_struct *mm, struct vm_area_struct **vma,
		unsigned int pages, struct page **hpage, int *progress) {
	unsigned long idx, addr, nr_pages, i;
	int ret;
	bool contpmd, cont, locked = false;
	struct coala_hint hint;

	/* loop through the hints */
	for (i = 0; i < COALA_HINT_IDXSZ; i++) {
		if (!locked && unlikely(!mmap_read_trylock(mm))) {
			return false;
		}

		locked = true;

		if (unlikely(khugepaged_test_exit(mm))) {
			return true;
		}

		if (khugepaged_scan.address != mm->coala_hints.epoch) {
			khugepaged_scan.address = mm->coala_hints.epoch;
			i = 0;
		}

		idx = mm->coala_hints.index[i];
		pr_debug("i, idx: %lu %lu", i, idx);
		if (!idx) {
			pr_debug("reached the end of index");
			break;
		}

		hint = coala_get_hint(mm, idx << PAGE_SHIFT);

		if (!hint.khugepaged_mark) {
			pr_debug("skipping hint %lu for 0x%lx with cleared mark", i, idx);
			continue;
		}

		addr = (idx << PAGE_SHIFT) & HPAGE_PMD_MASK;

		/* FIXME: VMA boundary checks */
		*vma = find_vma(mm, addr);
		if (!(*vma)) {
			pr_debug("vma is null for hint!");
			continue;
		}

		if (!hugepage_vma_check(*vma, (*vma)->vm_flags)) {
			pr_debug("vma not thp for hint!");
			continue;
		}

		nr_pages = HPAGE_PMD_NR;
		contpmd = (hint.val == COALA_HINT_32M);

		if (contpmd) {
			nr_pages *= CONT_PMDS;
		}

		/* FIXME: */
		if (find_vma(mm, addr + nr_pages * PAGE_SIZE) != *vma) {
			pr_debug("vma larger than hint!");
			continue;
		}

		if (contpmd) {
			cont = false;
			ret = coala_khugepaged_scan_contpmd(mm, *vma, addr, hpage, &cont);
			/* FIXME: khugepaged_hwk */
			if (cont) {
				continue;
			}

			if (!IS_ERR_OR_NULL(*hpage)) {
				pr_debug("freeing contpmd page");
				coala_khugepaged_free(*hpage);
				*hpage = NULL;
			}

			if (!ret) {
				int i = 0;
				pr_debug("contpmd promotion failed, falling back to 2m");
				for (i = 0; i < CONT_PMDS; i++, addr += HPAGE_PMD_SIZE) {
					ret = khugepaged_scan_pmd(mm, *vma, addr, hpage);
					if (!ret) {
						pmd_t *pmdp = mm_find_pmdp(mm, addr);
						pr_debug("page %d promotion failed, addr 0x%lx", i, addr);
						if (pmdp) {
							pr_debug("present: %d, transhuge: %d, pmd: 0x%llx",
									pmd_present(*pmdp), pmd_trans_huge(*pmdp),
									pmd_val(*pmdp));
						}

						continue;
					}

					if (!IS_ERR_OR_NULL(*hpage)) {
						pr_debug("freeing pmd page for fallback");
						put_page(*hpage);
						*hpage = NULL;
					}

					if (!khugepaged_hwk) {
						*progress += HPAGE_PMD_NR;
					}

					if (ret == 2) {
						if (khugepaged_hwk) {
							*progress += HPAGE_PMD_NR;
						}
						pr_debug("promotion 2m fallback success");	
					} else if (ret == 1) {
						pr_debug("failed fallback alloc?");
						if (khugepaged_hwk) {
							*progress += HPAGE_PMD_NR;
						}
					}
					mmap_read_lock(mm);
				}

				if (*progress >= pages) {
					return true;
				};

				mmap_read_unlock(mm);
				locked = false;
				continue;
			}
		} else if ((hint.val == COALA_HINT_64K) || (hint.val == COALA_HINT_2M)) {
			ret = khugepaged_scan_pmd(mm, *vma, addr, hpage);
		}

		if (!khugepaged_hwk) {
			*progress += nr_pages;
		}

		if (!IS_ERR_OR_NULL(*hpage)) {
			if (contpmd) {
				pr_debug("freeing contpmd page");
				coala_khugepaged_free(*hpage);
				*hpage = NULL;
			} else {
				pr_debug("freeing pmd page");
				put_page(*hpage);
				*hpage = NULL;
			}
		}

		if (ret) {
			if (khugepaged_hwk && ret == 2) {
				*progress += nr_pages;
				pr_debug("success? %d %u %lu", *progress, pages, nr_pages);
				if (*progress >= pages) {
					return false;
				}
			}

			if (ret == 1) {
				pr_debug("failed alloc? returning");
				*progress = pages;
				return false;
			}

			locked = false;	
			continue;
		}

		if (*progress >= pages) {
			return true;
		}

		if (i < COALA_HINT_IDXSZ - 1) {
			locked = false;
			mmap_read_unlock(mm);
		}
	}

	/* reset and move to the next mm */
	*vma = NULL;
	khugepaged_scan.address = 0;
	if (!locked && (*progress < pages) && mmap_read_trylock(mm)) {
		return true;
	}
	return locked;
}

#endif /* __LINUX_COALAPAGING_KHUGEPAGED_H */
