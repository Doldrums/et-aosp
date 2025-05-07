/* COALAPaging procfs stats */

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/mm_types.h>

#include "internal.h"
#include "stats.h"

atomic64_t total_contiguity_failure[NUM_CONTIG_FAILURES];
atomic64_t total_contiguity_success[NUM_CONTIG_SUCCESS];
atomic64_t total_alloc_hint;

atomic64_t contiguity_failure[MAX_ORDER][NUM_CONTIG_FAILURES];
atomic64_t contiguity_success[MAX_ORDER][NUM_CONTIG_SUCCESS];

static const char *contig_failures_str[] = {
	CONTIG_FAILURES(GENERATE_STR)
};
static const char *contig_success_str[] = {
	CONTIG_SUCCESS(GENERATE_STR)
};

atomic64_t pagehint_stats[NUM_PAGEHINT_STAT];

static const char *pagehint_stat_str[] = {
	PAGEHINT_STAT(GENERATE_STR)
};

atomic64_t migrated_pages;
atomic64_t contpmd_compact_success;
atomic64_t contpmd_compact_fail;

static int order_contiguity_failure_show(struct seq_file *m,void *v) {
	int i, order = (long)m->private;

	for (i = 0; i < NUM_CONTIG_FAILURES; i++)
		seq_printf(m, "%s:\t%lld\n", contig_failures_str[i],
			atomic64_read(&contiguity_failure[order][i]));

	return 0;
}

static int order_contiguity_success_show(struct seq_file *m,void *v) {
	int i, order = (long)m->private;

	for (i = 0; i < NUM_CONTIG_SUCCESS; i++)
		seq_printf(m, "%s:\t%lld\n", contig_success_str[i],
			atomic64_read(&contiguity_success[order][i]));

	return 0;
}

static int contiguity_failure_show(struct seq_file *m,void *v) {
	int i, order;

	for (order = 0; order < MAX_ORDER; order++) {
		seq_printf(m, "ORDER[%d]\n", order);
		for (i = 0; i < NUM_CONTIG_FAILURES; i++)
			seq_printf(m, "\t\t%s:\t%lld\n", contig_failures_str[i],
				atomic64_read(&contiguity_failure[order][i]));
	}

	seq_printf(m, "TOTAL\n");
	for (i = 0; i < NUM_CONTIG_FAILURES; i++)
		seq_printf(m, "\t\t%s:\t%lld\n", contig_failures_str[i],
			atomic64_read(&total_contiguity_failure[i]));

	return 0;
}

static int contiguity_success_show(struct seq_file *m,void *v) {
	int i, order;

	for (order = 0; order < MAX_ORDER; order++) {
		seq_printf(m, "ORDER[%d]\n", order);
		for (i = 0; i < NUM_CONTIG_SUCCESS; i++)
			seq_printf(m, "\t\t%s:\t%lld\n", contig_success_str[i],
				atomic64_read(&contiguity_success[order][i]));
	}

	seq_printf(m, "TOTAL\n");
	for (i = 0; i < NUM_CONTIG_SUCCESS; i++)
		seq_printf(m, "\t\t%s:\t%lld\n", contig_success_str[i],
			atomic64_read(&total_contiguity_success[i]));

	return 0;
}

static int total_contig_alloc_show(struct seq_file *m,void *v) {
	seq_printf(m, "%llu\n", atomic64_read(&total_alloc_hint));
	return 0;
}

static int pagehint_stats_show(struct seq_file *m,void *v) {
	int i;

	for (i = 0; i < NUM_PAGEHINT_STAT; i++)
		seq_printf(m, "%s:\t%lld\n", pagehint_stat_str[i],
			atomic64_read(&pagehint_stats[i]));
	return 0;
}

static int migrated_pages_show(struct seq_file *m,void *v) {
	seq_printf(m, "%llu\n", atomic64_read(&migrated_pages));
	return 0;
}

static int contpmd_compact_success_show(struct seq_file *m,void *v) {
	seq_printf(m, "%llu\n", atomic64_read(&contpmd_compact_success));
	return 0;
}

static int contpmd_compact_fail_show(struct seq_file *m,void *v) {
	seq_printf(m, "%llu\n", atomic64_read(&contpmd_compact_fail));
	return 0;
}

static int order_contiguity_failure_open(struct inode *inode, struct file *file) {
	return single_open(file, order_contiguity_failure_show,
			proc_get_parent_data(inode));
}

static int order_contiguity_success_open(struct inode *inode, struct file *file) {
	return single_open(file, order_contiguity_success_show,
			proc_get_parent_data(inode));
}

static int contiguity_failure_open(struct inode *inode, struct file *file) {
	return single_open(file, contiguity_failure_show, NULL);
}

static int contiguity_success_open(struct inode *inode, struct file *file) {
	return single_open(file, contiguity_success_show, NULL);
}

static int total_contig_alloc_open(struct inode *inode, struct file *file) {
	return single_open(file, total_contig_alloc_show, NULL);
}

static int pagehint_stats_open(struct inode *inode, struct file *file) {
	return single_open(file, pagehint_stats_show, NULL);
}

static int migrated_pages_open(struct inode *inode, struct file *file) {
	return single_open(file, migrated_pages_show, NULL);
}

static int contpmd_compact_success_open(struct inode *inode, struct file *file) {
	return single_open(file, contpmd_compact_success_show, NULL);
}

static int contpmd_compact_fail_open(struct inode *inode, struct file *file) {
	return single_open(file, contpmd_compact_fail_show, NULL);
}

static const struct proc_ops order_contiguity_failure_fops = {
	.proc_open = order_contiguity_failure_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops order_contiguity_success_fops = {
	.proc_open = order_contiguity_success_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops contiguity_failure_fops = {
	.proc_open = contiguity_failure_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops contiguity_success_fops = {
	.proc_open = contiguity_success_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops total_contig_alloc_fops = {
	.proc_open = total_contig_alloc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops pagehint_stats_fops = {
	.proc_open = pagehint_stats_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops migrated_pages_fops = {
	.proc_open = migrated_pages_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops contpmd_compact_success_fops = {
	.proc_open = contpmd_compact_success_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops contpmd_compact_fail_fops = {
	.proc_open = contpmd_compact_fail_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static ssize_t reset_stats_write(struct file *file, const char __user *buf,
					size_t count, loff_t *ppos) {
	int i, j;

	for (i = 0; i < NUM_CONTIG_FAILURES; i++) {
		atomic64_set(&total_contiguity_failure[i], 0);
		for (j = 0; j < MAX_ORDER; j++) {
			atomic64_set(&contiguity_failure[j][i], 0);
		}
	}

	for (i = 0; i < NUM_CONTIG_SUCCESS; i++) {
		atomic64_set(&total_contiguity_success[i], 0);
		for (j = 0; j < MAX_ORDER; j++) {
			atomic64_set(&contiguity_success[j][i], 0);
		}
	}

	atomic64_set(&total_alloc_hint, 0);

	for (i = 0; i < NUM_PAGEHINT_STAT; i++) {
		atomic64_set(&pagehint_stats[i], 0);
	}

	atomic64_set(&migrated_pages, 0);
	atomic64_set(&contpmd_compact_success, 0);
	atomic64_set(&contpmd_compact_fail, 0);

	return 0;
}

static const struct proc_ops reset_stats_fops = {
	.proc_write = reset_stats_write,
};

static ssize_t hints_active_read(struct file *file, char __user *buf,
					size_t count, loff_t *ppos) {
	char res[256];
	int n = min(count, (size_t)min(snprintf(res, 256, "%llu\n",
					atomic64_read(&coala_hints_active)), 256));

	if (*ppos) {
		return 0;
	}

	if (copy_to_user(buf, res, n)) {
		return -EFAULT;
	}

	*ppos += n;

	return n;
}

static ssize_t hints_active_write(struct file *file, const char __user *buf,
					size_t count, loff_t *ppos) {
	atomic64_set(&coala_hints_active, 0);
	return count;
}

static const struct proc_ops hints_active_fops = {
	.proc_read = hints_active_read,
	.proc_write = hints_active_write,
};

static int __init coala_procfs_stats_init(void) {
	int i;
	struct proc_dir_entry *stats_dir, *subdir;
	char buf[64];

	stats_dir = proc_mkdir_data("coalapaging", 0, NULL, NULL);
	proc_create("failure", 0, stats_dir, &contiguity_failure_fops);
	proc_create("success", 0, stats_dir, &contiguity_success_fops);
    proc_create("total_alloc", 0, stats_dir, &total_contig_alloc_fops);

	for (i = 0; i < MAX_ORDER; i++) {
		snprintf(buf, 64, "%i", i);
		subdir = proc_mkdir_data(buf, 0, stats_dir, (void *)((long)i));
		proc_create("failure", 0, subdir, &order_contiguity_failure_fops);
		proc_create("success", 0, subdir, &order_contiguity_success_fops);
	}

    proc_create("pagehint_stats", 0, stats_dir, &pagehint_stats_fops);
    proc_create("reset_stats", 0, stats_dir, &reset_stats_fops);

    proc_create("migrated_pages", 0, stats_dir, &migrated_pages_fops);
    proc_create("contpmd_compact_success", 0, stats_dir, &contpmd_compact_success_fops);
    proc_create("contpmd_compact_fail", 0, stats_dir, &contpmd_compact_fail_fops);

    proc_create("hints_active", 0, stats_dir, &hints_active_fops);

	return 0;
}

fs_initcall(coala_procfs_stats_init);
