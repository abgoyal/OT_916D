
#ifndef __ASM_OFFSETS_H__
#define __ASM_OFFSETS_H__

#define PT_orig_r10 4 /* offsetof(struct pt_regs, orig_r10) */
#define PT_r13 8 /* offsetof(struct pt_regs, r13) */
#define PT_r12 12 /* offsetof(struct pt_regs, r12) */
#define PT_r11 16 /* offsetof(struct pt_regs, r11) */
#define PT_r10 20 /* offsetof(struct pt_regs, r10) */
#define PT_r9 24 /* offsetof(struct pt_regs, r9) */
#define PT_mof 64 /* offsetof(struct pt_regs, mof) */
#define PT_dccr 68 /* offsetof(struct pt_regs, dccr) */
#define PT_srp 72 /* offsetof(struct pt_regs, srp) */

#define TI_task 0 /* offsetof(struct thread_info, task) */
#define TI_flags 8 /* offsetof(struct thread_info, flags) */
#define TI_preempt_count 16 /* offsetof(struct thread_info, preempt_count) */

#define THREAD_ksp 0 /* offsetof(struct thread_struct, ksp) */
#define THREAD_usp 4 /* offsetof(struct thread_struct, usp) */
#define THREAD_dccr 8 /* offsetof(struct thread_struct, dccr) */

#define TASK_pid 141 /* offsetof(struct task_struct, pid) */

#define LCLONE_VM 256 /* CLONE_VM */
#define LCLONE_UNTRACED 8388608 /* CLONE_UNTRACED */

#endif
