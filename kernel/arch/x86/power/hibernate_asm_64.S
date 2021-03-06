

	.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/asm-offsets.h>
#include <asm/processor-flags.h>

ENTRY(swsusp_arch_suspend)
	movq	$saved_context, %rax
	movq	%rsp, pt_regs_sp(%rax)
	movq	%rbp, pt_regs_bp(%rax)
	movq	%rsi, pt_regs_si(%rax)
	movq	%rdi, pt_regs_di(%rax)
	movq	%rbx, pt_regs_bx(%rax)
	movq	%rcx, pt_regs_cx(%rax)
	movq	%rdx, pt_regs_dx(%rax)
	movq	%r8, pt_regs_r8(%rax)
	movq	%r9, pt_regs_r9(%rax)
	movq	%r10, pt_regs_r10(%rax)
	movq	%r11, pt_regs_r11(%rax)
	movq	%r12, pt_regs_r12(%rax)
	movq	%r13, pt_regs_r13(%rax)
	movq	%r14, pt_regs_r14(%rax)
	movq	%r15, pt_regs_r15(%rax)
	pushfq
	popq	pt_regs_flags(%rax)

	/* save the address of restore_registers */
	movq	$restore_registers, %rax
	movq	%rax, restore_jump_address(%rip)
	/* save cr3 */
	movq	%cr3, %rax
	movq	%rax, restore_cr3(%rip)

	call swsusp_save
	ret

ENTRY(restore_image)
	/* switch to temporary page tables */
	movq	$__PAGE_OFFSET, %rdx
	movq	temp_level4_pgt(%rip), %rax
	subq	%rdx, %rax
	movq	%rax, %cr3
	/* Flush TLB */
	movq	mmu_cr4_features(%rip), %rax
	movq	%rax, %rdx
	andq	$~(X86_CR4_PGE), %rdx
	movq	%rdx, %cr4;  # turn off PGE
	movq	%cr3, %rcx;  # flush TLB
	movq	%rcx, %cr3;
	movq	%rax, %cr4;  # turn PGE back on

	/* prepare to jump to the image kernel */
	movq	restore_jump_address(%rip), %rax
	movq	restore_cr3(%rip), %rbx

	/* prepare to copy image data to their original locations */
	movq	restore_pblist(%rip), %rdx
	movq	relocated_restore_code(%rip), %rcx
	jmpq	*%rcx

	/* code below has been relocated to a safe page */
ENTRY(core_restore_code)
loop:
	testq	%rdx, %rdx
	jz	done

	/* get addresses from the pbe and copy the page */
	movq	pbe_address(%rdx), %rsi
	movq	pbe_orig_address(%rdx), %rdi
	movq	$(PAGE_SIZE >> 3), %rcx
	rep
	movsq

	/* progress to the next pbe */
	movq	pbe_next(%rdx), %rdx
	jmp	loop
done:
	/* jump to the restore_registers address from the image header */
	jmpq	*%rax
	/*
	 * NOTE: This assumes that the boot kernel's text mapping covers the
	 * image kernel's page containing restore_registers and the address of
	 * this page is the same as in the image kernel's text mapping (it
	 * should always be true, because the text mapping is linear, starting
	 * from 0, and is supposed to cover the entire kernel text for every
	 * kernel).
	 *
	 * code below belongs to the image kernel
	 */

ENTRY(restore_registers)
	/* go back to the original page tables */
	movq    %rbx, %cr3

	/* Flush TLB, including "global" things (vmalloc) */
	movq	mmu_cr4_features(%rip), %rax
	movq	%rax, %rdx
	andq	$~(X86_CR4_PGE), %rdx
	movq	%rdx, %cr4;  # turn off PGE
	movq	%cr3, %rcx;  # flush TLB
	movq	%rcx, %cr3
	movq	%rax, %cr4;  # turn PGE back on

	/* We don't restore %rax, it must be 0 anyway */
	movq	$saved_context, %rax
	movq	pt_regs_sp(%rax), %rsp
	movq	pt_regs_bp(%rax), %rbp
	movq	pt_regs_si(%rax), %rsi
	movq	pt_regs_di(%rax), %rdi
	movq	pt_regs_bx(%rax), %rbx
	movq	pt_regs_cx(%rax), %rcx
	movq	pt_regs_dx(%rax), %rdx
	movq	pt_regs_r8(%rax), %r8
	movq	pt_regs_r9(%rax), %r9
	movq	pt_regs_r10(%rax), %r10
	movq	pt_regs_r11(%rax), %r11
	movq	pt_regs_r12(%rax), %r12
	movq	pt_regs_r13(%rax), %r13
	movq	pt_regs_r14(%rax), %r14
	movq	pt_regs_r15(%rax), %r15
	pushq	pt_regs_flags(%rax)
	popfq

	xorq	%rax, %rax

	/* tell the hibernation core that we've just restored the memory */
	movq	%rax, in_suspend(%rip)

	ret
