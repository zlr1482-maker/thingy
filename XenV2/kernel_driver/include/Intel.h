#pragma once

//
// Intel.h - Intel VT-x / VMX constants, VMCS field encodings, and structure definitions.
// Reference: Intel SDM Vol 3C, Chapters 24-34 and Appendix A/B.
//

#include <ntdef.h>
#include <intrin.h>

// ============================================================
//  MSR addresses
// ============================================================

#define MSR_IA32_FEATURE_CONTROL     0x0000003A
#define MSR_IA32_SYSENTER_CS         0x00000174
#define MSR_IA32_SYSENTER_ESP        0x00000175
#define MSR_IA32_SYSENTER_EIP        0x00000176
#define MSR_IA32_DEBUGCTL            0x000001D9
#define MSR_IA32_PAT                 0x00000277
#define MSR_IA32_PERF_GLOBAL_CTRL    0x0000038F

#define MSR_IA32_VMX_BASIC           0x00000480
#define MSR_IA32_VMX_PINBASED_CTLS  0x00000481
#define MSR_IA32_VMX_PROCBASED_CTLS 0x00000482
#define MSR_IA32_VMX_EXIT_CTLS       0x00000483
#define MSR_IA32_VMX_ENTRY_CTLS      0x00000484
#define MSR_IA32_VMX_MISC            0x00000485
#define MSR_IA32_VMX_CR0_FIXED0      0x00000486
#define MSR_IA32_VMX_CR0_FIXED1      0x00000487
#define MSR_IA32_VMX_CR4_FIXED0      0x00000488
#define MSR_IA32_VMX_CR4_FIXED1      0x00000489
#define MSR_IA32_VMX_PROCBASED_CTLS2 0x0000048B
#define MSR_IA32_VMX_EPT_VPID_CAP   0x0000048C
#define MSR_IA32_VMX_TRUE_PINBASED  0x0000048D
#define MSR_IA32_VMX_TRUE_PROCBASED 0x0000048E
#define MSR_IA32_VMX_TRUE_EXIT_CTLS 0x0000048F
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS 0x00000490

#define MSR_IA32_EFER               0xC0000080
#define MSR_IA32_STAR               0xC0000081
#define MSR_IA32_LSTAR              0xC0000082
#define MSR_IA32_FMASK              0xC0000084
#define MSR_IA32_FS_BASE            0xC0000100
#define MSR_IA32_GS_BASE            0xC0000101
#define MSR_IA32_KERNEL_GS_BASE     0xC0000102

// IA32_FEATURE_CONTROL bits
#define FEATURE_CONTROL_LOCK_BIT              (1ULL << 0)
#define FEATURE_CONTROL_VMX_IN_SMX_ENABLED   (1ULL << 1)
#define FEATURE_CONTROL_VMX_OUTSIDE_SMX      (1ULL << 2)

// IA32_EFER bits
#define EFER_SCE   (1ULL << 0)   // SYSCALL Enable
#define EFER_LME   (1ULL << 8)   // Long Mode Enable
#define EFER_LMA   (1ULL << 10)  // Long Mode Active
#define EFER_NXE   (1ULL << 11)  // No-Execute Enable

// ============================================================
//  VMCS field encodings  (Intel SDM Vol 3C, Appendix B)
// ============================================================

// --- 16-bit Control ---
#define VMCS_VPID                            0x0000
#define VMCS_POSTED_INT_NOTIFICATION_VECTOR  0x0002
#define VMCS_EPTP_INDEX                      0x0004

// --- 16-bit Guest State ---
#define VMCS_GUEST_ES_SEL                    0x0800
#define VMCS_GUEST_CS_SEL                    0x0802
#define VMCS_GUEST_SS_SEL                    0x0804
#define VMCS_GUEST_DS_SEL                    0x0806
#define VMCS_GUEST_FS_SEL                    0x0808
#define VMCS_GUEST_GS_SEL                    0x080A
#define VMCS_GUEST_LDTR_SEL                  0x080C
#define VMCS_GUEST_TR_SEL                    0x080E
#define VMCS_GUEST_INT_STATUS                0x0810

// --- 16-bit Host State ---
#define VMCS_HOST_ES_SEL                     0x0C00
#define VMCS_HOST_CS_SEL                     0x0C02
#define VMCS_HOST_SS_SEL                     0x0C04
#define VMCS_HOST_DS_SEL                     0x0C06
#define VMCS_HOST_FS_SEL                     0x0C08
#define VMCS_HOST_GS_SEL                     0x0C0A
#define VMCS_HOST_TR_SEL                     0x0C0C

// --- 64-bit Control ---
#define VMCS_IO_BITMAP_A                     0x2000
#define VMCS_IO_BITMAP_B                     0x2002
#define VMCS_MSR_BITMAP                      0x2004
#define VMCS_VMEXIT_MSR_STORE_ADDR           0x2006
#define VMCS_VMEXIT_MSR_LOAD_ADDR            0x2008
#define VMCS_VMENTRY_MSR_LOAD_ADDR           0x200A
#define VMCS_TSC_OFFSET                      0x2010
#define VMCS_EPTP                            0x201A

// --- 64-bit Read-Only ---
#define VMCS_GUEST_PHYSICAL_ADDR             0x2400  // EPT violation: guest-physical address

// --- 64-bit Guest State ---
#define VMCS_GUEST_VMCS_LINK_PTR             0x2800
#define VMCS_GUEST_IA32_DEBUGCTL             0x2802
#define VMCS_GUEST_IA32_PAT                  0x2804
#define VMCS_GUEST_IA32_EFER                 0x2806
#define VMCS_GUEST_IA32_PERF_GLOBAL_CTRL     0x2808
#define VMCS_GUEST_PDPTE0                    0x280A
#define VMCS_GUEST_PDPTE1                    0x280C
#define VMCS_GUEST_PDPTE2                    0x280E
#define VMCS_GUEST_PDPTE3                    0x2810

// --- 64-bit Host State ---
#define VMCS_HOST_IA32_PAT                   0x2C00
#define VMCS_HOST_IA32_EFER                  0x2C02
#define VMCS_HOST_IA32_PERF_GLOBAL_CTRL      0x2C04

// --- 32-bit Control ---
#define VMCS_PIN_BASED_CTLS                  0x4000
#define VMCS_PROC_BASED_CTLS                 0x4002
#define VMCS_EXCEPTION_BITMAP                0x4004
#define VMCS_PAGE_FAULT_ERR_MASK             0x4006
#define VMCS_PAGE_FAULT_ERR_MATCH            0x4008
#define VMCS_CR3_TARGET_COUNT                0x400A
#define VMCS_VMEXIT_CTLS                     0x400C
#define VMCS_VMEXIT_MSR_STORE_COUNT          0x400E
#define VMCS_VMEXIT_MSR_LOAD_COUNT           0x4010
#define VMCS_VMENTRY_CTLS                    0x4012
#define VMCS_VMENTRY_MSR_LOAD_COUNT          0x4014
#define VMCS_VMENTRY_INT_INFO_FIELD          0x4016
#define VMCS_VMENTRY_XCPT_ERR_CODE           0x4018
#define VMCS_VMENTRY_INSTR_LEN               0x401A
#define VMCS_TPR_THRESHOLD                   0x401C
#define VMCS_PROC_BASED_CTLS2                0x401E
#define VMCS_PLE_GAP                         0x4020
#define VMCS_PLE_WINDOW                      0x4022

// --- 32-bit Read-Only ---
#define VMCS_VM_INSTR_ERROR                  0x4400
#define VMCS_EXIT_REASON                     0x4402
#define VMCS_EXIT_INT_INFO                   0x4404
#define VMCS_EXIT_INT_ERR_CODE               0x4406
#define VMCS_IDT_VECTORING_INFO              0x4408
#define VMCS_IDT_VECTORING_ERR_CODE          0x440A
#define VMCS_EXIT_INSTR_LEN                  0x440C
#define VMCS_EXIT_INSTR_INFO                 0x440E

// --- 32-bit Guest State ---
#define VMCS_GUEST_ES_LIMIT                  0x4800
#define VMCS_GUEST_CS_LIMIT                  0x4802
#define VMCS_GUEST_SS_LIMIT                  0x4804
#define VMCS_GUEST_DS_LIMIT                  0x4806
#define VMCS_GUEST_FS_LIMIT                  0x4808
#define VMCS_GUEST_GS_LIMIT                  0x480A
#define VMCS_GUEST_LDTR_LIMIT                0x480C
#define VMCS_GUEST_TR_LIMIT                  0x480E
#define VMCS_GUEST_GDTR_LIMIT                0x4810
#define VMCS_GUEST_IDTR_LIMIT                0x4812
#define VMCS_GUEST_ES_ACCESS                 0x4814
#define VMCS_GUEST_CS_ACCESS                 0x4816
#define VMCS_GUEST_SS_ACCESS                 0x4818
#define VMCS_GUEST_DS_ACCESS                 0x481A
#define VMCS_GUEST_FS_ACCESS                 0x481C
#define VMCS_GUEST_GS_ACCESS                 0x481E
#define VMCS_GUEST_LDTR_ACCESS               0x4820
#define VMCS_GUEST_TR_ACCESS                 0x4822
#define VMCS_GUEST_INT_STATE                 0x4824
#define VMCS_GUEST_ACTIVITY_STATE            0x4826
#define VMCS_GUEST_SMBASE                    0x4828
#define VMCS_GUEST_IA32_SYSENTER_CS          0x482A
#define VMCS_GUEST_VMX_PREEMPT_TIMER         0x482E

// --- 32-bit Host State ---
#define VMCS_HOST_IA32_SYSENTER_CS           0x4C00

// --- Natural-Width Control ---
#define VMCS_CR0_GUEST_HOST_MASK             0x6000
#define VMCS_CR4_GUEST_HOST_MASK             0x6002
#define VMCS_CR0_READ_SHADOW                 0x6004
#define VMCS_CR4_READ_SHADOW                 0x6006
#define VMCS_CR3_TARGET0                     0x6008
#define VMCS_CR3_TARGET1                     0x600A
#define VMCS_CR3_TARGET2                     0x600C
#define VMCS_CR3_TARGET3                     0x600E

// --- Natural-Width Read-Only ---
#define VMCS_EXIT_QUAL                       0x6400
#define VMCS_IO_RCX                          0x6402
#define VMCS_IO_RSI                          0x6404
#define VMCS_IO_RDI                          0x6406
#define VMCS_IO_RIP                          0x6408
#define VMCS_GUEST_LINEAR_ADDR               0x640A

// --- Natural-Width Guest State ---
#define VMCS_GUEST_CR0                       0x6800
#define VMCS_GUEST_CR3                       0x6802
#define VMCS_GUEST_CR4                       0x6804
#define VMCS_GUEST_ES_BASE                   0x6806
#define VMCS_GUEST_CS_BASE                   0x6808
#define VMCS_GUEST_SS_BASE                   0x680A
#define VMCS_GUEST_DS_BASE                   0x680C
#define VMCS_GUEST_FS_BASE                   0x680E
#define VMCS_GUEST_GS_BASE                   0x6810
#define VMCS_GUEST_LDTR_BASE                 0x6812
#define VMCS_GUEST_TR_BASE                   0x6814
#define VMCS_GUEST_GDTR_BASE                 0x6816
#define VMCS_GUEST_IDTR_BASE                 0x6818
#define VMCS_GUEST_DR7                       0x681A
#define VMCS_GUEST_RSP                       0x681C
#define VMCS_GUEST_RIP                       0x681E
#define VMCS_GUEST_RFLAGS                    0x6820
#define VMCS_GUEST_PENDING_DBG_XCPT          0x6822
#define VMCS_GUEST_IA32_SYSENTER_ESP         0x6824
#define VMCS_GUEST_IA32_SYSENTER_EIP         0x6826

// --- Natural-Width Host State ---
#define VMCS_HOST_CR0                        0x6C00
#define VMCS_HOST_CR3                        0x6C02
#define VMCS_HOST_CR4                        0x6C04
#define VMCS_HOST_FS_BASE                    0x6C06
#define VMCS_HOST_GS_BASE                    0x6C08
#define VMCS_HOST_TR_BASE                    0x6C0A
#define VMCS_HOST_GDTR_BASE                  0x6C0C
#define VMCS_HOST_IDTR_BASE                  0x6C0E
#define VMCS_HOST_IA32_SYSENTER_ESP          0x6C10
#define VMCS_HOST_IA32_SYSENTER_EIP          0x6C12
#define VMCS_HOST_RSP                        0x6C14
#define VMCS_HOST_RIP                        0x6C16

// ============================================================
//  VM exit reasons (Intel SDM Vol 3C, Appendix C)
// ============================================================

#define VMX_EXIT_XCPT_OR_NMI          0
#define VMX_EXIT_EXT_INTR             1
#define VMX_EXIT_TRIPLE_FAULT         2
#define VMX_EXIT_INIT_SIGNAL          3
#define VMX_EXIT_SIPI                 4
#define VMX_EXIT_IO_SMI               5
#define VMX_EXIT_OTHER_SMI            6
#define VMX_EXIT_INTR_WINDOW          7
#define VMX_EXIT_NMI_WINDOW           8
#define VMX_EXIT_TASK_SWITCH          9
#define VMX_EXIT_CPUID               10
#define VMX_EXIT_GETSEC              11
#define VMX_EXIT_HLT                 12
#define VMX_EXIT_INVD                13
#define VMX_EXIT_INVLPG              14
#define VMX_EXIT_RDPMC               15
#define VMX_EXIT_RDTSC               16
#define VMX_EXIT_RSM                 17
#define VMX_EXIT_VMCALL              18
#define VMX_EXIT_VMCLEAR             19
#define VMX_EXIT_VMLAUNCH            20
#define VMX_EXIT_VMPTRLD             21
#define VMX_EXIT_VMPTRST             22
#define VMX_EXIT_VMREAD              23
#define VMX_EXIT_VMRESUME            24
#define VMX_EXIT_VMWRITE             25
#define VMX_EXIT_VMXOFF              26
#define VMX_EXIT_VMXON               27
#define VMX_EXIT_CR_ACCESS           28
#define VMX_EXIT_MOV_DR              29
#define VMX_EXIT_IO_INSTR            30
#define VMX_EXIT_RDMSR               31
#define VMX_EXIT_WRMSR               32
#define VMX_EXIT_ENTRY_FAIL_GUEST    33
#define VMX_EXIT_ENTRY_FAIL_MSR      34
#define VMX_EXIT_MWAIT               36
#define VMX_EXIT_MTF                 37
#define VMX_EXIT_MONITOR             39
#define VMX_EXIT_PAUSE               40
#define VMX_EXIT_ENTRY_FAIL_MCE      41
#define VMX_EXIT_TPR_BELOW_THRESHOLD 43
#define VMX_EXIT_APIC_ACCESS         44
#define VMX_EXIT_VIRTUALIZED_EOI     45
#define VMX_EXIT_GDTR_IDTR_ACCESS    46
#define VMX_EXIT_LDTR_TR_ACCESS      47
#define VMX_EXIT_EPT_VIOLATION       48
#define VMX_EXIT_EPT_MISCONFIG       49
#define VMX_EXIT_INVEPT              50
#define VMX_EXIT_RDTSCP              51
#define VMX_EXIT_VMX_PREEMPT_TIMER   52
#define VMX_EXIT_INVVPID             53
#define VMX_EXIT_WBINVD              54
#define VMX_EXIT_XSETBV              55
#define VMX_EXIT_APIC_WRITE          56
#define VMX_EXIT_RDRAND              57
#define VMX_EXIT_INVPCID             58
#define VMX_EXIT_VMFUNC              59
#define VMX_EXIT_ENCLS               60
#define VMX_EXIT_RDSEED              61
#define VMX_EXIT_XSAVES              63
#define VMX_EXIT_XRSTORS             64

// ============================================================
//  Pin-based VM-execution control bits
// ============================================================
#define PIN_BASED_EXT_INTR_EXIT      (1u << 0)
#define PIN_BASED_NMI_EXIT           (1u << 3)
#define PIN_BASED_VIRTUAL_NMIS       (1u << 5)
#define PIN_BASED_VMX_PREEMPT_TIMER  (1u << 6)
#define PIN_BASED_POSTED_INTERRUPTS  (1u << 7)

// ============================================================
//  Primary processor-based VM-execution control bits
// ============================================================
#define CPU_BASED_INTR_WINDOW_EXIT   (1u << 2)
#define CPU_BASED_USE_TSC_OFFSET     (1u << 3)
#define CPU_BASED_HLT_EXIT           (1u << 7)
#define CPU_BASED_INVLPG_EXIT        (1u << 9)
#define CPU_BASED_MWAIT_EXIT         (1u << 10)
#define CPU_BASED_RDPMC_EXIT         (1u << 11)
#define CPU_BASED_RDTSC_EXIT         (1u << 12)
#define CPU_BASED_CR3_LOAD_EXIT      (1u << 15)
#define CPU_BASED_CR3_STORE_EXIT     (1u << 16)
#define CPU_BASED_CR8_LOAD_EXIT      (1u << 19)
#define CPU_BASED_CR8_STORE_EXIT     (1u << 20)
#define CPU_BASED_TPR_SHADOW         (1u << 21)
#define CPU_BASED_NMI_WINDOW_EXIT    (1u << 22)
#define CPU_BASED_MOV_DR_EXIT        (1u << 23)
#define CPU_BASED_UNCOND_IO_EXIT     (1u << 24)
#define CPU_BASED_USE_IO_BITMAP      (1u << 25)
#define CPU_BASED_MONITOR_TRAP_FLAG  (1u << 27)
#define CPU_BASED_USE_MSR_BITMAP     (1u << 28)
#define CPU_BASED_MONITOR_EXIT       (1u << 29)
#define CPU_BASED_PAUSE_EXIT         (1u << 30)
#define CPU_BASED_ACTIVATE_CTLS2     (1u << 31)

// ============================================================
//  Secondary processor-based VM-execution control bits
// ============================================================
#define CPU_BASED2_VIRTUALIZE_APIC   (1u << 0)
#define CPU_BASED2_ENABLE_EPT        (1u << 1)
#define CPU_BASED2_DESC_TABLE_EXIT   (1u << 2)
#define CPU_BASED2_ENABLE_RDTSCP     (1u << 3)
#define CPU_BASED2_VIRTUALIZE_X2APIC (1u << 4)
#define CPU_BASED2_ENABLE_VPID       (1u << 5)
#define CPU_BASED2_WBINVD_EXIT       (1u << 6)
#define CPU_BASED2_UNRESTRICTED_GUEST (1u << 7)
#define CPU_BASED2_APIC_REG_VIRT     (1u << 8)
#define CPU_BASED2_VIRT_INTR_DELIVERY (1u << 9)
#define CPU_BASED2_PAUSE_LOOP_EXIT   (1u << 10)
#define CPU_BASED2_RDRAND_EXIT       (1u << 11)
#define CPU_BASED2_ENABLE_INVPCID    (1u << 12)
#define CPU_BASED2_ENABLE_VMFUNC     (1u << 13)
#define CPU_BASED2_VMCS_SHADOWING    (1u << 14)
#define CPU_BASED2_RDSEED_EXIT       (1u << 16)
#define CPU_BASED2_ENABLE_PML        (1u << 17)
#define CPU_BASED2_EPT_VIOLATION_VE  (1u << 18)
#define CPU_BASED2_XSAVE_XRSTOR      (1u << 20)
#define CPU_BASED2_MODE_BASED_EPT_X  (1u << 22)
#define CPU_BASED2_USE_TSC_SCALING   (1u << 25)

// ============================================================
//  VM-exit control bits
// ============================================================
#define VM_EXIT_SAVE_DBG_CTLS        (1u << 2)
#define VM_EXIT_HOST_ADDR_SPACE_SIZE (1u << 9)   // 64-bit host
#define VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL (1u << 12)
#define VM_EXIT_ACK_INTR_ON_EXIT     (1u << 15)
#define VM_EXIT_SAVE_IA32_PAT        (1u << 18)
#define VM_EXIT_LOAD_IA32_PAT        (1u << 19)
#define VM_EXIT_SAVE_IA32_EFER       (1u << 20)
#define VM_EXIT_LOAD_IA32_EFER       (1u << 21)
#define VM_EXIT_SAVE_VMX_PREEMPT_TIMER (1u << 22)
#define VM_EXIT_CLEAR_BNDCFGS        (1u << 23)

// ============================================================
//  VM-entry control bits
// ============================================================
#define VM_ENTRY_LOAD_DBG_CTLS       (1u << 2)
#define VM_ENTRY_IA32E_MODE_GUEST    (1u << 9)   // 64-bit guest
#define VM_ENTRY_SMM                 (1u << 10)
#define VM_ENTRY_DEACT_DUAL_MONITOR  (1u << 11)
#define VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL (1u << 13)
#define VM_ENTRY_LOAD_IA32_PAT       (1u << 14)
#define VM_ENTRY_LOAD_IA32_EFER      (1u << 15)
#define VM_ENTRY_LOAD_BNDCFGS        (1u << 16)
#define VM_ENTRY_CONCEAL_VMX_PT      (1u << 17)

// ============================================================
//  Segment access-rights helpers
// ============================================================
#pragma pack(push, 1)

union SegmentAccessRights {
    struct {
        UINT32 Type       : 4;
        UINT32 System     : 1;  // 0 = system, 1 = code/data
        UINT32 Dpl        : 2;
        UINT32 Present    : 1;
        UINT32 Reserved0  : 4;
        UINT32 Avl        : 1;
        UINT32 Long       : 1;  // 64-bit code segment
        UINT32 Db         : 1;  // Default operand size
        UINT32 Granularity: 1;
        UINT32 Unusable   : 1;  // Segment unusable
        UINT32 Reserved1  : 15;
    };
    UINT32 Value;
};

union SegmentDescriptor {
    struct {
        UINT64 LimitLow   : 16;
        UINT64 BaseLow    : 16;
        UINT64 BaseMid    : 8;
        UINT64 Type       : 4;
        UINT64 System     : 1;
        UINT64 Dpl        : 2;
        UINT64 Present    : 1;
        UINT64 LimitHigh  : 4;
        UINT64 Avl        : 1;
        UINT64 Long       : 1;
        UINT64 Db         : 1;
        UINT64 Granularity: 1;
        UINT64 BaseHigh   : 8;
    };
    UINT64 Value;
};

struct GdtRegister {
    UINT16 Limit;
    UINT64 Base;
};

struct IdtRegister {
    UINT16 Limit;
    UINT64 Base;
};

// EPT pointer (EPTP) layout
union EptPointer {
    struct {
        UINT64 MemType     : 3;  // 6 = Write-back
        UINT64 PageWalkLen : 3;  // 3 = 4-level paging (value = walk length - 1)
        UINT64 AccessDirty : 1;
        UINT64 Reserved0   : 5;
        UINT64 Pfn         : 36;
        UINT64 Reserved1   : 16;
    };
    UINT64 Value;
};

#pragma pack(pop)

// ============================================================
//  EPT paging structures
// ============================================================

#pragma pack(push, 1)

union EptPml4Entry {
    struct {
        UINT64 Read        : 1;
        UINT64 Write       : 1;
        UINT64 Execute     : 1;
        UINT64 Reserved0   : 5;
        UINT64 Accessed    : 1;
        UINT64 Reserved1   : 1;
        UINT64 UserExecute : 1;
        UINT64 Reserved2   : 1;
        UINT64 Pfn         : 36;
        UINT64 Reserved3   : 16;
    };
    UINT64 Value;
};

union EptPdptEntry {
    struct {
        UINT64 Read        : 1;
        UINT64 Write       : 1;
        UINT64 Execute     : 1;
        UINT64 Reserved0   : 5;
        UINT64 Accessed    : 1;
        UINT64 Reserved1   : 1;
        UINT64 UserExecute : 1;
        UINT64 Reserved2   : 1;
        UINT64 Pfn         : 36;
        UINT64 Reserved3   : 16;
    };
    UINT64 Value;
};

// 2MB large-page EPT entry (Intel SDM Vol 3C §29.3.3).
// CRITICAL: bits 20:12 are reserved MBZ for large pages — setting any of them
// triggers EPT misconfiguration (VM-exit reason 49).  The PFN of a 2MB entry
// sits at bits 51:21 (not bits 51:12 like a 4KB entry).
union EptPdEntry {
    struct {
        UINT64 Read        : 1;  // bit  0
        UINT64 Write       : 1;  // bit  1
        UINT64 Execute     : 1;  // bit  2
        UINT64 MemType     : 3;  // bits 3-5
        UINT64 IgnorePat   : 1;  // bit  6
        UINT64 LargePage   : 1;  // bit  7 — must be 1 for 2MB
        UINT64 Accessed    : 1;  // bit  8
        UINT64 Dirty       : 1;  // bit  9
        UINT64 UserExecute : 1;  // bit 10
        UINT64 Ignored0    : 1;  // bit 11
        UINT64 ReservedMbz : 9;  // bits 12-20 — MBZ for 2MB page
        UINT64 Pfn         : 31; // bits 21-51 — 2MB-aligned physical address
        UINT64 Reserved1   : 10; // bits 52-61
        UINT64 Ignored1    : 1;  // bit 62
        UINT64 SuppressVe  : 1;  // bit 63
    };
    UINT64 Value;
};

union EptPtEntry {
    struct {
        UINT64 Read        : 1;
        UINT64 Write       : 1;
        UINT64 Execute     : 1;
        UINT64 MemType     : 3;  // EPT memory type (0=UC, 6=WB)
        UINT64 IgnorePat   : 1;
        UINT64 Reserved0   : 1;
        UINT64 Accessed    : 1;
        UINT64 Dirty       : 1;
        UINT64 UserExecute : 1;
        UINT64 Reserved1   : 1;
        UINT64 Pfn         : 36;
        UINT64 Reserved2   : 15;
        UINT64 SuppressVe  : 1;
    };
    UINT64 Value;
};

#pragma pack(pop)

// EPT memory types
#define EPT_MT_UC  0   // Uncacheable
#define EPT_MT_WC  1   // Write-combining
#define EPT_MT_WT  4   // Write-through
#define EPT_MT_WP  5   // Write-protected
#define EPT_MT_WB  6   // Write-back (use for normal RAM)

// EPT access bits
#define EPT_ACCESS_READ    (1ULL << 0)
#define EPT_ACCESS_WRITE   (1ULL << 1)
#define EPT_ACCESS_EXECUTE (1ULL << 2)
#define EPT_ACCESS_ALL     (EPT_ACCESS_READ | EPT_ACCESS_WRITE | EPT_ACCESS_EXECUTE)

// ============================================================
//  VMCALL hypercall codes
// ============================================================
#define HYPERCALL_PING          0x5800  // Sanity check
#define HYPERCALL_READ_MEMORY   0x5801
#define HYPERCALL_WRITE_MEMORY  0x5802
#define HYPERCALL_VMXOFF        0x5803  // Graceful shutdown

// ============================================================
//  Inline helpers
// ============================================================

// Read a VMCS field — wraps MSVC intrinsic
inline UINT64 VmcsRead(UINT64 field) {
    UINT64 val = 0;
    __vmx_vmread(field, &val);
    return val;
}

// Write a VMCS field
inline void VmcsWrite(UINT64 field, UINT64 value) {
    __vmx_vmwrite(field, value);
}

// Apply VMX fixed bits: ensure required-1 bits are set, required-0 bits are clear
inline UINT64 AdjustControls(UINT64 desired, UINT32 msr) {
    UINT64 msrVal = __readmsr(msr);
    UINT32 low    = (UINT32)(msrVal & 0xFFFFFFFF);   // Bits that must be 1
    UINT32 high   = (UINT32)(msrVal >> 32);           // Bits that may be 1
    desired |= low;
    desired &= high;
    return desired;
}
