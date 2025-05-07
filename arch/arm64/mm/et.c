#include <linux/kstrtox.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>

#include "../../fs/proc/internal.h"

bool et_global = false;			/* ET globally enabled */
bool et_batched = false;		/* batch flush optimization (FIXME: not implemented) */
bool et_flush_full = true;		/* strictly follow BBM wrt TLB flushes */
bool et_cacheline_opt = true;	/* access the range entries in cacheline chunks */
bool et_validate = false;		/* extra validations for ranges */

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "et."

module_param_named(global, et_global, bool, 0644);
module_param_named(batched, et_batched, bool, 0644);
module_param_named(flush_full, et_flush_full, bool, 0644);
module_param_named(cacheline_opt, et_cacheline_opt, bool, 0644);
module_param_named(validate_ranges, et_validate, bool, 0644);

/* procfs knob to read the ET status for a process */
static ssize_t et_proc_read(struct file *file, char __user *buf,
                       size_t count, loff_t *ppos) {
	struct task_struct *task = get_proc_task(file_inode(file));
	char buffer[PROC_NUMBUF];
	size_t len;
	int ret;

	if (!task)
		return -ESRCH;

   BUG_ON(!task->mm);

   ret = task->mm->et_enabled;
   put_task_struct(task);
   len = snprintf(buffer, sizeof(buffer), "%hd\n", ret);
   return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct file_operations et_proc_ops = {
       .read           = et_proc_read,
       .llseek         = default_llseek,
};
