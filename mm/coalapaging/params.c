#include <linux/moduleparam.h>
#include "../internal.h"
#include "./internal.h"
#include <linux/coalapaging.h>

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "coalapaging."

/* enable COALAPaging allocations for page-cache */
bool coala_pagecache = false;
module_param_named(pagecache, coala_pagecache, bool, 0644);

/* Fragmentation threashold for default THP allocations */
unsigned long coala_frag_thresh = 0;
module_param_named(frag_thresh, coala_frag_thresh, ulong, 0644);

/* enable stats */
bool coala_stats = false;
module_param_named(stats, coala_stats, bool, 0644);

/* allow khugepaged to defrag COALA-enabled mm's */
bool coala_khugepaged = false;
module_param_named(khugepaged, coala_khugepaged, bool, 0644);

/* enable / disable kcompactd */
bool coala_kcompactd = true;
module_param_named(kcompactd, coala_kcompactd, bool, 0644);

/* enable / disable hints at fault */
bool coala_fault_hints = true;
module_param_named(fault_hints, coala_fault_hints, bool, 0644);

/* fallback to normal khugepaged when no khuge hints active */
bool coala_khuge_fallback = false;
module_param_named(khuge_fallback, coala_khuge_fallback, bool, 0644);

/* async promotion to 32M */
bool coala_khuge_etheap_async = false;
module_param_named(khuge_etheap_async, coala_khuge_etheap_async, bool, 0644);

/* khuge task round-robin */
bool coala_khuge_rr= false;
module_param_named(khuge_rr, coala_khuge_rr, bool, 0644);
