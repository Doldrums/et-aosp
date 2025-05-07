/* included in compaction.c */
#ifndef __LINUX_COALAPAGING_COMPACTION_H
#define __LINUX_COALAPAGING_COMPACTION_H

extern bool coala_kcompactd;
extern atomic64_t migrated_pages;
extern atomic64_t contpmd_compact_success;
extern atomic64_t contpmd_compact_fail;

static bool compact_lock_irqsave(spinlock_t *lock, unsigned long *flags,
						struct compact_control *cc);
static bool suitable_migration_source(struct compact_control *cc,
							struct page *page);

/* possible outcome of isolate_migratepages */
typedef enum {
	ISOLATE_ABORT,		/* Abort compaction now */
	ISOLATE_NONE,		/* No pages isolated, continue scanning */
	ISOLATE_SUCCESS,	/* Pages isolated, migrate */
} isolate_migrate_t;

static inline bool is_via_etreclaim(gfp_t gfp_mask) {
	return gfp_mask & __GFP_ETRECLAIM;
}

static inline void etreclaim_free(struct page *page,
		struct compact_control *cc) {
	int order = 0;
	if (PageCompound(page)) {
		order = compound_order(page);
		if (order && !PageHead(page)) {
			pr_crit("wtf pagehead free");
		}
	} else {
		order = page_private(page);
	}

	list_add(&page->lru, &cc->freepages);
	set_page_private(page, order);
	cc->nr_freepages += 1UL << order;
	pr_debug("freeing order-%d 0x%lx ", order, (unsigned long)page);
}

static inline struct page *etreclaim_alloc(struct page *migratepage,
		struct compact_control *cc) {
	bool compound;
	struct page *freepage, *target = NULL, *page;
	int migrate_order, target_order = -1, order;

 	compound = PageCompound(migratepage);
	if (compound) {
		pr_debug("migrating compound page %d", compound_order(migratepage));
		if (PageTail(migratepage)) {
			pr_crit("wtf tail");
		}
	} else if (compound_order(migratepage) != 0) {
		pr_crit("wtf non-compound order %d", compound_order(migratepage));
	}

	migrate_order = compound_order(migratepage);

	list_for_each_entry(freepage, &cc->freepages, lru) {
		order = page_private(freepage);
		pr_debug("freepage 0x%lx %d", (unsigned long)freepage, order);
		if (order >= migrate_order && (target_order < 0 || order < target_order)) {
			target_order = order;
			target = freepage;
		}
	}

	if (!target) {
		pr_debug("no suitable freepage found %d", target_order);
		return NULL;
	}

	list_del(&target->lru);
	pr_debug("removing from freelist, %d", list_empty(&cc->freepages));

	order = target_order;
	while (order-- > migrate_order) {
		page = target + (1UL << order);
		set_page_private(page, order);
		list_add(&page->lru, &cc->freepages);
		pr_debug("adding to free list, order: %d, orig: %d, %d", order, order + 1, list_empty(&cc->freepages));
	}

	if (compound) {
		post_alloc_hook(target, migrate_order, __GFP_MOVABLE | __GFP_COMP);
		prep_compound_page(target, migrate_order);
		if (PageTransHuge(migratepage)) {
			prep_transhuge_page(target);
		}
	} else {
		post_alloc_hook(target, 0, __GFP_MOVABLE);
	}

	cc->nr_freepages -= (1UL << migrate_order);

	pr_debug("migrate_order: %d, target_order: %d, target: 0x%lx freepages: %u",
			migrate_order, target_order, (unsigned long)target, cc->nr_freepages);

	return target;
}

static inline isolate_migrate_t
etreclaim_isolate_migratepages(struct compact_control *cc) {
	unsigned long block_start_pfn, block_end_pfn, pfn;
	struct lruvec *locked;
	struct lruvec *lruvec;
	unsigned long nr_isolated, nr_block, flags = 0;
	struct address_space *mapping;
	struct page *page;

	block_start_pfn = IS_ALIGNED(cc->migrate_pfn, MAX_ORDER_NR_PAGES)
		? cc->migrate_pfn : ALIGN(cc->migrate_pfn, MAX_ORDER_NR_PAGES);
	block_end_pfn = block_start_pfn + MAX_ORDER_NR_PAGES;

	if (block_start_pfn < cc->zone->zone_start_pfn) {
		pr_crit("wtf start pfn 0x%lx", block_start_pfn);
		return ISOLATE_NONE;
	}

	cc->migrate_pfn = block_start_pfn;

#if 1
	pr_debug("scan starting, migrate_pfn: 0x%lx, free_pfn: 0x%lx, zone_end: 0x%lx",
			cc->migrate_pfn, cc->free_pfn, zone_end_pfn(cc->zone));
#endif

	for (; block_end_pfn <= cc->free_pfn;
			cc->migrate_pfn = block_start_pfn = block_end_pfn,
			block_end_pfn += MAX_ORDER_NR_PAGES) {
		page = pageblock_pfn_to_page(block_start_pfn, block_end_pfn, cc->zone);
		if (!page) {
			//pr_debug("pageblock pfn to page fail");
			continue;
		}

		if (!suitable_migration_source(cc, page)) {
			//pr_debug("suitable migration fail");
			continue;
		}

		nr_isolated = 0;
		nr_block = 0;

		for (pfn = block_start_pfn; pfn < block_end_pfn; pfn += HPAGE_PMD_NR) {
			if (!IS_ALIGNED(pfn, HPAGE_PMD_NR)) {
				pr_crit("wtf misaligned pfn");
			}

			if (!PageTransHuge(pfn_to_page(pfn))) {
				break;
			}
		}

		if (pfn >= block_end_pfn) {
			pr_debug("only thp pages, skipping block");
			continue;
		}

		for (pfn = block_start_pfn; pfn < block_end_pfn; pfn++) {
			bool compound;
			unsigned long flags = 0;

			page = pfn_to_page(pfn);
			if (PageBuddy(page)) {
				unsigned long freepage_order, nr_freepages, flags;

				spin_lock_irqsave(&cc->zone->lock, flags);

				if (!PageBuddy(page)) {
					spin_unlock_irqrestore(&cc->zone->lock, flags);
					pr_debug("page buddy retracted");
					break;
				}

				freepage_order = buddy_order(page);
				nr_freepages = 1UL << freepage_order;

				__isolate_free_page(page, freepage_order);

				if (!pfn_valid(page_to_pfn(page))) {
					pr_crit("wtf invalid pfn for isofree 0x%lx", page_to_pfn(page));
					BUG();
				}

				pfn += nr_freepages - 1;
				nr_block += nr_freepages;

				set_page_private(page, freepage_order);
				list_add(&page->lru, &cc->isofreepages);

				cc->nr_isofreepages += nr_freepages;

				spin_unlock_irqrestore(&cc->zone->lock, flags);

				continue;
			}

			if (PageLeshy(page)) {
				pr_debug("skipping page leshy");
				break;
			}

			compound = PageCompound(page);
			if (compound) {
				pr_debug("trying to isolate compound page");
				if (PageTail(page)) {
					page = compound_head(page);
					pfn = page_to_pfn(page);
				}
			}

			if (!PageLRU(page)) {
				if (unlikely(__PageMovable(page)) && !PageIsolated(page)) {
					if (locked) {
						unlock_page_lruvec_irqrestore(locked, flags);
						locked = NULL;
					}

					if (!isolate_movable_page(page, MIGRATE_SYNC)) {
						goto isolate_success;
					}
				}

				pr_debug("page lru unmovable fail");

				break;
			}

			mapping = page_mapping(page);
			if (!mapping && page_count(page) > page_mapcount(page)) {
				pr_debug("page pinned fail");
				break;
			}

			if (!(cc->gfp_mask & __GFP_FS) && mapping) {
				pr_debug("page GFP_FS fail");
				break;
			}

			if (unlikely(!get_page_unless_zero(page))) {
				pr_debug("page get fail");
				break;
			}

			if (!PageLRU(page)) {
				pr_debug("page not lru fail");
				goto isolate_fail_put;
			}

			if (PageUnevictable(page)) {
				pr_debug("page unevicable fail");
				goto isolate_fail_put;
			}

			if (!TestClearPageLRU(page)) {
				pr_debug("page clearlru fail");
				goto isolate_fail_put;
			}

			lruvec = folio_lruvec(page_folio(page));
			if (lruvec != locked) {
				if (locked) {
					unlock_page_lruvec_irqrestore(locked, flags);
				}

				compact_lock_irqsave(&lruvec->lru_lock, &flags, cc);
				locked = lruvec;
			}

			del_page_from_lru_list(page, lruvec);
isolate_success:
			if (!pfn_valid(page_to_pfn(page))) {
				pr_crit("wtf invalid pfn for migrate 0x%lx", page_to_pfn(page));
				goto isolate_fail_put;
			}

			list_add(&page->lru, &cc->migratepages);
			cc->nr_migratepages += compound_nr(page);
			nr_isolated += compound_nr(page);
			nr_block += compound_nr(page);

			if (compound_nr(page) > 1) {
				pr_debug("isolated %ld", compound_nr(page));
				pfn += compound_nr(page) - 1;
			}

			mod_node_page_state(page_pgdat(page),
					NR_ISOLATED_ANON + page_is_file_lru(page),
					thp_nr_pages(page));

			continue;

isolate_fail_put:
			if (locked) {
				unlock_page_lruvec_irqrestore(locked, flags);
				locked = NULL;
			}
			put_page(page);
			break;
		}

		if (locked) {
			unlock_page_lruvec_irqrestore(locked, flags);
			locked = NULL;
		}

		if (nr_block != MAX_ORDER_NR_PAGES) {
			putback_movable_pages(&cc->migratepages);

			cc->nr_migratepages = 0;
			cc->nr_isofreepages = 0;

			release_freepages(&cc->isofreepages, true);
		} else  {
			pr_debug("scan success, isolated: %lu, block: %lu", nr_isolated, nr_block);
			cc->migrate_pfn = block_end_pfn;
			return ISOLATE_SUCCESS;
		}
	}

	pr_info("scan fail");
	cc->migrate_pfn = block_end_pfn;
	return ISOLATE_NONE;
}

static inline void etreclaim_finish_compact(struct compact_control *cc) {
	struct page *page, *next;
	unsigned long start;
	int i;
	bool check[MAX_ORDER_NR_PAGES];
	struct capture_control *capc = current->capture_control;

	memset(check, 0, sizeof(bool) * MAX_ORDER_NR_PAGES);

	if (!list_empty(&cc->migratepages)) {
		start = page_to_pfn(list_entry(cc->migratepages.next, struct page, lru));
		if (!pfn_valid(start)) {
			pr_crit("wtf migrate before alignment 0x%lx 0x%lx", start,
					(unsigned long)pfn_to_page(start));
		}
		if (!IS_ALIGNED(start, MAX_ORDER_NR_PAGES)) {
			start = ALIGN_DOWN(start, MAX_ORDER_NR_PAGES);
		}
		if (!pfn_valid(start)) {
			pr_crit("wtf migrate after alignment 0x%lx 0x%lx", start, 
					(unsigned long)pfn_to_page(start));
		}
	} else if (!list_empty(&cc->isofreepages)) {
		start = page_to_pfn(list_entry(cc->isofreepages.next, struct page, lru));
		if (!pfn_valid(start)) {
			pr_crit("wtf isofree before alignment 0x%lx 0x%lx", start,
					(unsigned long)pfn_to_page(start));
		}
		if (!IS_ALIGNED(start, MAX_ORDER_NR_PAGES)) {
			start = ALIGN_DOWN(start, MAX_ORDER_NR_PAGES);
		}
		if (!pfn_valid(start)) {
			pr_crit("wtf isofree after alignment 0x%lx 0x%lx", start, 
					(unsigned long)pfn_to_page(start));
		}
	} else {
		pr_crit("wtf, both empty");
		return;
	}

	list_for_each_entry_safe(page, next, &cc->isofreepages, lru) {
		unsigned long order = page_private(page);
		list_del(&page->lru);

		for (i = 0; i < (1UL << order); i++) {
			check[(page_to_pfn(page) % MAX_ORDER_NR_PAGES) + i] = true;

			if (PageMappingFlags(page)) {
				page->mapping = NULL;
			}

			page_cpupid_reset_last(page);
			page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
			reset_page_owner(page, order);
		}
	}

	list_for_each_entry_safe(page, next, &cc->migratepages, lru) {
		unsigned long order = compound_order(page);
		list_del(&page->lru);

		for (i = 0; i < (1UL << order); i++) {
			check[(page_to_pfn(page) % MAX_ORDER_NR_PAGES) + i] = true;
		}

		if (!atomic_dec_and_test(&page->_refcount)) {
			dump_page(page, "refcount");
			BUG();
		}

		if (PageMappingFlags(page)) {
			page->mapping = NULL;
		}
		__mem_cgroup_uncharge(page_folio(page));

		page_cpupid_reset_last(page);
		page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
		reset_page_owner(page, order);

		atomic64_add((1UL << order), &migrated_pages);
	}

	page = pfn_to_page(start);

	set_page_private(page, MAX_ORDER - 1);
	set_page_count(page, 0);
	set_page_owner(page, MAX_ORDER - 1, cc->gfp_mask);

	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;

	capc->page = page;

	atomic64_inc(&contpmd_compact_success);
}

static inline void etreclaim_putback_free(struct compact_control *cc) {
	atomic64_inc(&contpmd_compact_fail);

	cc->nr_isofreepages = 0;
	release_freepages(&cc->isofreepages, true);
}
#endif /* __LINUX_COALAPAGING_ALLOC_H */
