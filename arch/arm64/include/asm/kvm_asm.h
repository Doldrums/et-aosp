/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM_KVM_ASM_H__
#define __ARM_KVM_ASM_H__

#include <asm/hyp_image.h>
#include <asm/insn.h>
#include <asm/virt.h>

#define ARM_EXIT_WITH_SERROR_BIT  31
#define ARM_EXCEPTION_CODE(x)	  ((x) & ~(1U << ARM_EXIT_WITH_SERROR_BIT))
#define ARM_EXCEPTION_IS_TRAP(x)  (ARM_EXCEPTION_CODE((x)) == ARM_EXCEPTION_TRAP)
#define ARM_SERROR_PENDING(x)	  !!((x) & (1U << ARM_EXIT_WITH_SERROR_BIT))

#define ARM_EXCEPTION_IRQ	  0
#define ARM_EXCEPTION_EL1_SERROR  1
#define ARM_EXCEPTION_TRAP	  2
#define ARM_EXCEPTION_IL	  3
/* The hyp-stub will return this for any kvm_call_hyp() call */
#define ARM_EXCEPTION_HYP_GONE	  HVC_STUB_ERR

#define kvm_arm_exception_type					\
	{ARM_EXCEPTION_IRQ,		"IRQ"		},	\
	{ARM_EXCEPTION_EL1_SERROR, 	"SERROR"	},	\
	{ARM_EXCEPTION_TRAP, 		"TRAP"		},	\
	{ARM_EXCEPTION_HYP_GONE,	"HYP_GONE"	}

/*
 * Size of the HYP vectors preamble. kvm_patch_vector_branch() generates code
 * that jumps over this.
 */
#define KVM_VECTOR_PREAMBLE	(2 * AARCH64_INSN_SIZE)

#define KVM_HOST_SMCCC_ID(id)						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   (id))

#define KVM_HOST_SMCCC_FUNC(name) KVM_HOST_SMCCC_ID(__KVM_HOST_SMCCC_FUNC_##name)

#define __KVM_HOST_SMCCC_FUNC___kvm_hyp_init			0

#ifndef __ASSEMBLY__

#include <linux/mm.h>

enum __kvm_host_smccc_func {
	/* Hypercalls available only prior to pKVM finalisation */
	/* __KVM_HOST_SMCCC_FUNC___kvm_hyp_init */
	__KVM_HOST_SMCCC_FUNC___kvm_get_mdcr_el2 = __KVM_HOST_SMCCC_FUNC___kvm_hyp_init + 1,
	__KVM_HOST_SMCCC_FUNC___pkvm_init,
	__KVM_HOST_SMCCC_FUNC___pkvm_create_private_mapping,
	__KVM_HOST_SMCCC_FUNC___pkvm_cpu_set_vector,
	__KVM_HOST_SMCCC_FUNC___kvm_enable_ssbs,
	__KVM_HOST_SMCCC_FUNC___vgic_v3_init_lrs,
	__KVM_HOST_SMCCC_FUNC___vgic_v3_get_gic_config,
	__KVM_HOST_SMCCC_FUNC___kvm_flush_vm_context,
	__KVM_HOST_SMCCC_FUNC___kvm_tlb_flush_vmid_ipa,
	__KVM_HOST_SMCCC_FUNC___kvm_tlb_flush_vmid,
	__KVM_HOST_SMCCC_FUNC___kvm_flush_cpu_context,
	__KVM_HOST_SMCCC_FUNC___pkvm_alloc_module_va,
	__KVM_HOST_SMCCC_FUNC___pkvm_map_module_page,
	__KVM_HOST_SMCCC_FUNC___pkvm_unmap_module_page,
	__KVM_HOST_SMCCC_FUNC___pkvm_init_module,
	__KVM_HOST_SMCCC_FUNC___pkvm_register_hcall,
	__KVM_HOST_SMCCC_FUNC___pkvm_prot_finalize,

	/* Hypercalls available after pKVM finalisation */
	__KVM_HOST_SMCCC_FUNC___pkvm_host_share_hyp,
	__KVM_HOST_SMCCC_FUNC___pkvm_host_unshare_hyp,
	__KVM_HOST_SMCCC_FUNC___pkvm_host_map_guest,
	__KVM_HOST_SMCCC_FUNC___kvm_adjust_pc,
	__KVM_HOST_SMCCC_FUNC___kvm_vcpu_run,
	__KVM_HOST_SMCCC_FUNC___kvm_timer_set_cntvoff,
	__KVM_HOST_SMCCC_FUNC___vgic_v3_save_vmcr_aprs,
	__KVM_HOST_SMCCC_FUNC___vgic_v3_restore_vmcr_aprs,
	__KVM_HOST_SMCCC_FUNC___pkvm_init_vm,
	__KVM_HOST_SMCCC_FUNC___pkvm_init_vcpu,
	__KVM_HOST_SMCCC_FUNC___pkvm_start_teardown_vm,
	__KVM_HOST_SMCCC_FUNC___pkvm_finalize_teardown_vm,
	__KVM_HOST_SMCCC_FUNC___pkvm_reclaim_dying_guest_page,
	__KVM_HOST_SMCCC_FUNC___pkvm_vcpu_load,
	__KVM_HOST_SMCCC_FUNC___pkvm_vcpu_put,
	__KVM_HOST_SMCCC_FUNC___pkvm_vcpu_sync_state,
	__KVM_HOST_SMCCC_FUNC___pkvm_iommu_driver_init,
	__KVM_HOST_SMCCC_FUNC___pkvm_iommu_register,
	__KVM_HOST_SMCCC_FUNC___pkvm_iommu_pm_notify,
	__KVM_HOST_SMCCC_FUNC___pkvm_iommu_finalize,
	__KVM_HOST_SMCCC_FUNC___pkvm_load_tracing,
	__KVM_HOST_SMCCC_FUNC___pkvm_teardown_tracing,
	__KVM_HOST_SMCCC_FUNC___pkvm_enable_tracing,
	__KVM_HOST_SMCCC_FUNC___pkvm_rb_swap_reader_page,
	__KVM_HOST_SMCCC_FUNC___pkvm_rb_update_footers,
	__KVM_HOST_SMCCC_FUNC___pkvm_enable_event,
#ifdef CONFIG_ANDROID_ARM64_WORKAROUND_DMA_BEYOND_POC
	__KVM_HOST_SMCCC_FUNC___pkvm_host_set_stage2_memattr,
#endif
	__KVM_HOST_SMCCC_FUNC___kvm_tlb_flush_range,

	/*
	 * Start of the dynamically registered hypercalls. Start a bit
	 * further, just in case some modules...
	 */
	__KVM_HOST_SMCCC_FUNC___dynamic_hcalls = 128,
};

#define DECLARE_KVM_VHE_SYM(sym)	extern char sym[]
#define DECLARE_KVM_NVHE_SYM(sym)	extern char kvm_nvhe_sym(sym)[]

/*
 * Define a pair of symbols sharing the same name but one defined in
 * VHE and the other in nVHE hyp implementations.
 */
#define DECLARE_KVM_HYP_SYM(sym)		\
	DECLARE_KVM_VHE_SYM(sym);		\
	DECLARE_KVM_NVHE_SYM(sym)

#define DECLARE_KVM_VHE_PER_CPU(type, sym)	\
	DECLARE_PER_CPU(type, sym)
#define DECLARE_KVM_NVHE_PER_CPU(type, sym)	\
	DECLARE_PER_CPU(type, kvm_nvhe_sym(sym))

#define DECLARE_KVM_HYP_PER_CPU(type, sym)	\
	DECLARE_KVM_VHE_PER_CPU(type, sym);	\
	DECLARE_KVM_NVHE_PER_CPU(type, sym)

/*
 * Compute pointer to a symbol defined in nVHE percpu region.
 * Returns NULL if percpu memory has not been allocated yet.
 */
#define this_cpu_ptr_nvhe_sym(sym)	per_cpu_ptr_nvhe_sym(sym, smp_processor_id())
#define per_cpu_ptr_nvhe_sym(sym, cpu)						\
	({									\
		unsigned long base, off;					\
		base = kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[cpu];		\
		off = (unsigned long)&CHOOSE_NVHE_SYM(sym) -			\
		      (unsigned long)&CHOOSE_NVHE_SYM(__per_cpu_start);		\
		base ? (typeof(CHOOSE_NVHE_SYM(sym))*)(base + off) : NULL;	\
	})

#if defined(__KVM_NVHE_HYPERVISOR__)

#define CHOOSE_NVHE_SYM(sym)	sym
#define CHOOSE_HYP_SYM(sym)	CHOOSE_NVHE_SYM(sym)

/* The nVHE hypervisor shouldn't even try to access VHE symbols */
extern void *__nvhe_undefined_symbol;
#define CHOOSE_VHE_SYM(sym)		__nvhe_undefined_symbol
#define this_cpu_ptr_hyp_sym(sym)	(&__nvhe_undefined_symbol)
#define per_cpu_ptr_hyp_sym(sym, cpu)	(&__nvhe_undefined_symbol)

/*
 * pKVM uses the module_ops struct to expose services to modules but
 * doesn't allow fine-grained definition of the license for each export,
 * and doesn't have a way to check the license of the loaded module.
 * Given that said module may be proprietary, let's seek GPL compliance
 * by preventing the accidental export of GPL symbols to hyp modules via
 * pKVM's module_ops struct.
 */
#ifdef EXPORT_SYMBOL_GPL
#undef EXPORT_SYMBOL_GPL
#endif
#define EXPORT_SYMBOL_GPL(sym) BUILD_BUG()

#elif defined(__KVM_VHE_HYPERVISOR__)

#define CHOOSE_VHE_SYM(sym)	sym
#define CHOOSE_HYP_SYM(sym)	CHOOSE_VHE_SYM(sym)

/* The VHE hypervisor shouldn't even try to access nVHE symbols */
extern void *__vhe_undefined_symbol;
#define CHOOSE_NVHE_SYM(sym)		__vhe_undefined_symbol
#define this_cpu_ptr_hyp_sym(sym)	(&__vhe_undefined_symbol)
#define per_cpu_ptr_hyp_sym(sym, cpu)	(&__vhe_undefined_symbol)

#else

/*
 * BIG FAT WARNINGS:
 *
 * - Don't be tempted to change the following is_kernel_in_hyp_mode()
 *   to has_vhe(). has_vhe() is implemented as a *final* capability,
 *   while this is used early at boot time, when the capabilities are
 *   not final yet....
 *
 * - Don't let the nVHE hypervisor have access to this, as it will
 *   pick the *wrong* symbol (yes, it runs at EL2...).
 */
#define CHOOSE_HYP_SYM(sym)		(is_kernel_in_hyp_mode()	\
					   ? CHOOSE_VHE_SYM(sym)	\
					   : CHOOSE_NVHE_SYM(sym))

#define this_cpu_ptr_hyp_sym(sym)	(is_kernel_in_hyp_mode()	\
					   ? this_cpu_ptr(&sym)		\
					   : this_cpu_ptr_nvhe_sym(sym))

#define per_cpu_ptr_hyp_sym(sym, cpu)	(is_kernel_in_hyp_mode()	\
					   ? per_cpu_ptr(&sym, cpu)	\
					   : per_cpu_ptr_nvhe_sym(sym, cpu))

#define CHOOSE_VHE_SYM(sym)	sym
#define CHOOSE_NVHE_SYM(sym)	kvm_nvhe_sym(sym)

#endif

struct kvm_nvhe_init_params {
	unsigned long mair_el2;
	unsigned long tcr_el2;
	unsigned long tpidr_el2;
	unsigned long stack_hyp_va;
	unsigned long stack_pa;
	phys_addr_t pgd_pa;
	unsigned long hcr_el2;
	unsigned long hfgwtr_el2;
	unsigned long vttbr;
	unsigned long vtcr;
};

/*
 * Used by the host in EL1 to dump the nVHE hypervisor backtrace on
 * hyp_panic() in non-protected mode.
 *
 * @stack_base:                 hyp VA of the hyp_stack base.
 * @overflow_stack_base:        hyp VA of the hyp_overflow_stack base.
 * @fp:                         hyp FP where the backtrace begins.
 * @pc:                         hyp PC where the backtrace begins.
 */
struct kvm_nvhe_stacktrace_info {
	unsigned long stack_base;
	unsigned long overflow_stack_base;
	unsigned long fp;
	unsigned long pc;
};

/* Translate a kernel address @ptr into its equivalent linear mapping */
#define kvm_ksym_ref(ptr)						\
	({								\
		void *val = (ptr);					\
		if (!is_kernel_in_hyp_mode())				\
			val = lm_alias((ptr));				\
		val;							\
	 })
#define kvm_ksym_ref_nvhe(sym)	kvm_ksym_ref(kvm_nvhe_sym(sym))

struct kvm;
struct kvm_vcpu;
struct kvm_s2_mmu;

DECLARE_KVM_NVHE_SYM(__kvm_hyp_init);
DECLARE_KVM_HYP_SYM(__kvm_hyp_vector);
#define __kvm_hyp_init		CHOOSE_NVHE_SYM(__kvm_hyp_init)
#define __kvm_hyp_vector	CHOOSE_HYP_SYM(__kvm_hyp_vector)

extern unsigned long kvm_nvhe_sym(kvm_arm_hyp_percpu_base)[];
DECLARE_KVM_NVHE_SYM(__per_cpu_start);
DECLARE_KVM_NVHE_SYM(__per_cpu_end);

extern unsigned long kvm_nvhe_sym(kvm_arm_hyp_host_fp_state)[];

DECLARE_KVM_HYP_SYM(__bp_harden_hyp_vecs);
#define __bp_harden_hyp_vecs	CHOOSE_HYP_SYM(__bp_harden_hyp_vecs)

extern void __kvm_flush_vm_context(void);
extern void __kvm_flush_cpu_context(struct kvm_s2_mmu *mmu);
extern void __kvm_tlb_flush_vmid_ipa(struct kvm_s2_mmu *mmu, phys_addr_t ipa,
				     int level);
extern void __kvm_tlb_flush_vmid(struct kvm_s2_mmu *mmu);

#ifdef CONFIG_HAVE_ARCH_ELASTIC_TRANSLATIONS
/*
 * __tcr_flush_tlb_range() adapted for KVM
 */
extern void __kvm_tlb_flush_range(struct kvm_s2_mmu *mmu, phys_addr_t ipa,
					int level);
#endif /* CONFIG_HAVE_ARCH_ELASTIC_TRANSLATIONS */

extern void __kvm_timer_set_cntvoff(u64 cntvoff);

extern int __kvm_vcpu_run(struct kvm_vcpu *vcpu);

extern void __kvm_adjust_pc(struct kvm_vcpu *vcpu);

extern u64 __vgic_v3_get_gic_config(void);
extern void __vgic_v3_init_lrs(void);

extern u64 __kvm_get_mdcr_el2(void);

#define __KVM_EXTABLE(from, to)						\
	"	.pushsection	__kvm_ex_table, \"a\"\n"		\
	"	.align		3\n"					\
	"	.long		(" #from " - .), (" #to " - .)\n"	\
	"	.popsection\n"


#define __kvm_at(at_op, addr)						\
( { 									\
	int __kvm_at_err = 0;						\
	u64 spsr, elr;							\
	asm volatile(							\
	"	mrs	%1, spsr_el2\n"					\
	"	mrs	%2, elr_el2\n"					\
	"1:	at	"at_op", %3\n"					\
	"	isb\n"							\
	"	b	9f\n"						\
	"2:	msr	spsr_el2, %1\n"					\
	"	msr	elr_el2, %2\n"					\
	"	mov	%w0, %4\n"					\
	"9:\n"								\
	__KVM_EXTABLE(1b, 2b)						\
	: "+r" (__kvm_at_err), "=&r" (spsr), "=&r" (elr)		\
	: "r" (addr), "i" (-EFAULT));					\
	__kvm_at_err;							\
} )


#else /* __ASSEMBLY__ */

.macro get_host_ctxt reg, tmp
	adr_this_cpu \reg, kvm_host_data, \tmp
	add	\reg, \reg, #HOST_DATA_CONTEXT
.endm

.macro get_vcpu_ptr vcpu, ctxt
	get_host_ctxt \ctxt, \vcpu
	ldr	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
.endm

.macro get_loaded_vcpu vcpu, ctxt
	adr_this_cpu \ctxt, kvm_hyp_ctxt, \vcpu
	ldr	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
.endm

.macro set_loaded_vcpu vcpu, ctxt, tmp
	adr_this_cpu \ctxt, kvm_hyp_ctxt, \tmp
	str	\vcpu, [\ctxt, #HOST_CONTEXT_VCPU]
.endm

/*
 * KVM extable for unexpected exceptions.
 * Create a struct kvm_exception_table_entry output to a section that can be
 * mapped by EL2. The table is not sorted.
 *
 * The caller must ensure:
 * x18 has the hypervisor value to allow any Shadow-Call-Stack instrumented
 * code to write to it, and that SPSR_EL2 and ELR_EL2 are restored by the fixup.
 */
.macro	_kvm_extable, from, to
	.pushsection	__kvm_ex_table, "a"
	.align		3
	.long		(\from - .), (\to - .)
	.popsection
.endm

#define CPU_XREG_OFFSET(x)	(CPU_USER_PT_REGS + 8*x)
#define CPU_LR_OFFSET		CPU_XREG_OFFSET(30)
#define CPU_SP_EL0_OFFSET	(CPU_LR_OFFSET + 8)

/*
 * We treat x18 as callee-saved as the host may use it as a platform
 * register (e.g. for shadow call stack).
 */
.macro save_callee_saved_regs ctxt
	str	x18,      [\ctxt, #CPU_XREG_OFFSET(18)]
	stp	x19, x20, [\ctxt, #CPU_XREG_OFFSET(19)]
	stp	x21, x22, [\ctxt, #CPU_XREG_OFFSET(21)]
	stp	x23, x24, [\ctxt, #CPU_XREG_OFFSET(23)]
	stp	x25, x26, [\ctxt, #CPU_XREG_OFFSET(25)]
	stp	x27, x28, [\ctxt, #CPU_XREG_OFFSET(27)]
	stp	x29, lr,  [\ctxt, #CPU_XREG_OFFSET(29)]
.endm

.macro restore_callee_saved_regs ctxt
	// We require \ctxt is not x18-x28
	ldr	x18,      [\ctxt, #CPU_XREG_OFFSET(18)]
	ldp	x19, x20, [\ctxt, #CPU_XREG_OFFSET(19)]
	ldp	x21, x22, [\ctxt, #CPU_XREG_OFFSET(21)]
	ldp	x23, x24, [\ctxt, #CPU_XREG_OFFSET(23)]
	ldp	x25, x26, [\ctxt, #CPU_XREG_OFFSET(25)]
	ldp	x27, x28, [\ctxt, #CPU_XREG_OFFSET(27)]
	ldp	x29, lr,  [\ctxt, #CPU_XREG_OFFSET(29)]
.endm

.macro save_sp_el0 ctxt, tmp
	mrs	\tmp,	sp_el0
	str	\tmp,	[\ctxt, #CPU_SP_EL0_OFFSET]
.endm

.macro restore_sp_el0 ctxt, tmp
	ldr	\tmp,	  [\ctxt, #CPU_SP_EL0_OFFSET]
	msr	sp_el0, \tmp
.endm

#endif

#endif /* __ARM_KVM_ASM_H__ */
