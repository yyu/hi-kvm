#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>

/* CR0 bits */
#define CR0_PE 1u
#define CR0_MP (1U << 1)
#define CR0_EM (1U << 2)
#define CR0_TS (1U << 3)
#define CR0_ET (1U << 4)
#define CR0_NE (1U << 5)
#define CR0_WP (1U << 16)
#define CR0_AM (1U << 18)
#define CR0_NW (1U << 29)
#define CR0_CD (1U << 30)
#define CR0_PG (1U << 31)

/* CR4 bits */
#define CR4_VME 1
#define CR4_PVI (1U << 1)
#define CR4_TSD (1U << 2)
#define CR4_DE (1U << 3)
#define CR4_PSE (1U << 4)
#define CR4_PAE (1U << 5)
#define CR4_MCE (1U << 6)
#define CR4_PGE (1U << 7)
#define CR4_PCE (1U << 8)
#define CR4_OSFXSR (1U << 8)
#define CR4_OSXMMEXCPT (1U << 10)
#define CR4_UMIP (1U << 11)
#define CR4_VMXE (1U << 13)
#define CR4_SMXE (1U << 14)
#define CR4_FSGSBASE (1U << 16)
#define CR4_PCIDE (1U << 17)
#define CR4_OSXSAVE (1U << 18)
#define CR4_SMEP (1U << 20)
#define CR4_SMAP (1U << 21)

#define EFER_SCE 1
#define EFER_LME (1U << 8)
#define EFER_LMA (1U << 10)
#define EFER_NXE (1U << 11)

/* 32-bit page directory entry bits */
#define PDE32_PRESENT 1
#define PDE32_RW (1U << 1)
#define PDE32_USER (1U << 2)
#define PDE32_PS (1U << 7)

/* 64-bit page * entry bits */
#define PDE64_PRESENT 1
#define PDE64_RW (1U << 1)
#define PDE64_USER (1U << 2)
#define PDE64_ACCESSED (1U << 5)
#define PDE64_DIRTY (1U << 6)
#define PDE64_PS (1U << 7)
#define PDE64_G (1U << 8)


struct vm {
	int sys_fd;
	int fd;
	char *mem;
};

void vm_init(struct vm *vm, size_t mem_size)
{
	struct kvm_userspace_memory_region memreg;

	vm->sys_fd = open("/dev/kvm", O_RDWR);

	vm->fd = ioctl(vm->sys_fd, KVM_CREATE_VM, 0);

        ioctl(vm->fd, KVM_SET_TSS_ADDR, 0xfffbd000);

	vm->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	madvise(vm->mem, mem_size, MADV_MERGEABLE);

	memreg.slot = 0;
	memreg.flags = 0;
	memreg.guest_phys_addr = 0;
	memreg.memory_size = mem_size;
	memreg.userspace_addr = (unsigned long)vm->mem;

        ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &memreg);
}

struct vcpu {
	int fd;
	struct kvm_run *kvm_run;
};

void vcpu_init(struct vm *vm, struct vcpu *vcpu)
{
	int vcpu_mmap_size;

	vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, 0);

	vcpu_mmap_size = ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	vcpu->kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
}

int run_vm(struct vm *vm, struct vcpu *vcpu, size_t sz)
{
	struct kvm_regs regs;
	uint64_t memval = 0;

	for (;;) {
		ioctl(vcpu->fd, KVM_RUN, 0);

		switch (vcpu->kvm_run->exit_reason) {
		case KVM_EXIT_HLT:
			goto check;

		case KVM_EXIT_IO:
			if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT && vcpu->kvm_run->io.port == 0xE9) {
				char *p = (char *)vcpu->kvm_run;
				fwrite(p + vcpu->kvm_run->io.data_offset, vcpu->kvm_run->io.size, 1, stdout);
				fflush(stdout);
				continue;
			}

			/* fall through */
		default:
			fprintf(stderr,	"Got exit_reason %d, expected KVM_EXIT_HLT (%d)\n", vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
			exit(1);
		}
	}

 check:
	ioctl(vcpu->fd, KVM_GET_REGS, &regs);
        assert(regs.rax == 42);
	memcpy(&memval, &vm->mem[0x400], sz);
        assert(memval == 42);
	return 1;
}

extern const unsigned char guest16[], guest16_end[];

int run_real_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

        ioctl(vcpu->fd, KVM_GET_SREGS, &sregs);
	sregs.cs.selector = 0;
	sregs.cs.base = 0;
        ioctl(vcpu->fd, KVM_SET_SREGS, &sregs);

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;
	ioctl(vcpu->fd, KVM_SET_REGS, &regs);

	memcpy(vm->mem, guest16, guest16_end-guest16);
	return run_vm(vm, vcpu, 2);
}

int main()
{
	struct vm vm;
	struct vcpu vcpu;

	vm_init(&vm, 0x200000);
	vcpu_init(&vm, &vcpu);
	return !run_real_mode(&vm, &vcpu);
}

