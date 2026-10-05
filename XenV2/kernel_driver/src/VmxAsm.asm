; VmxAsm.asm - MASM x64 assembly stubs for XenV2-Type2
;
; Assembled by MASM (ml64.exe) via the WDK build system.
; Calling convention: Windows x64 (RCX, RDX, R8, R9, then stack; shadow space 32 bytes).

EXTERN VmExitHandler : PROC

; ============================================================
;  GuestRegisters layout (must match Vmx.h struct GuestRegisters):
;   RSP+0x00  R15
;   RSP+0x08  R14
;   RSP+0x10  R13
;   RSP+0x18  R12
;   RSP+0x20  R11
;   RSP+0x28  R10
;   RSP+0x30  R9
;   RSP+0x38  R8
;   RSP+0x40  RDI
;   RSP+0x48  RSI
;   RSP+0x50  RBP
;   RSP+0x58  (RSP placeholder — actual guest RSP is in VMCS)
;   RSP+0x60  RBX
;   RSP+0x68  RDX
;   RSP+0x70  RCX
;   RSP+0x78  RAX
; ============================================================

.code

; ------------------------------------------------------------
;  VmExitStub
;
;  Called as the host RIP on every VM exit (VMCS_HOST_RIP).
;  Host RSP = VMCS_HOST_RSP (top of per-core 32KB stack, 16-byte aligned).
;
;  Flow:
;    1. Push all 16 GPRs onto the host stack  (128 bytes).
;    2. RCX = RSP (pointer to GuestRegisters).
;    3. sub rsp, 20h  (32-byte shadow space; 128+32=160 — 16-byte aligned).
;    4. call VmExitHandler  (C++, returns BOOLEAN: 0=vmresume, 1=vmxoff path).
;    5. Restore all GPRs.
;    6. If handler returned 0 → vmresume; else → ret (for VMXOFF path).
; ------------------------------------------------------------
PUBLIC VmExitStub
VmExitStub PROC
    ; Save guest GPRs (128 bytes)
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rbp         ; RSP placeholder slot
    push    rbp         ; actual RBP
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    ; Save XMM0..XMM15 (256 bytes, 16-byte aligned since RSP is 16-aligned here).
    ; Without this the C exit handler's SSE/AVX usage corrupts guest XMM state
    ; and the guest's next XRSTORS reads a torn buffer -> #GP -> BSOD.
    sub     rsp, 100h
    movaps  [rsp + 000h], xmm0
    movaps  [rsp + 010h], xmm1
    movaps  [rsp + 020h], xmm2
    movaps  [rsp + 030h], xmm3
    movaps  [rsp + 040h], xmm4
    movaps  [rsp + 050h], xmm5
    movaps  [rsp + 060h], xmm6
    movaps  [rsp + 070h], xmm7
    movaps  [rsp + 080h], xmm8
    movaps  [rsp + 090h], xmm9
    movaps  [rsp + 0A0h], xmm10
    movaps  [rsp + 0B0h], xmm11
    movaps  [rsp + 0C0h], xmm12
    movaps  [rsp + 0D0h], xmm13
    movaps  [rsp + 0E0h], xmm14
    movaps  [rsp + 0F0h], xmm15

    lea     rcx, [rsp + 100h]   ; arg1 = pointer to saved GuestRegisters (past XMM area)
    sub     rsp, 20h             ; shadow space (stack still 16-byte aligned: 128+256+32=416)

    call    VmExitHandler        ; returns BOOLEAN in AL/RAX; we ignore it

    add     rsp, 20h

    ; Restore XMMs
    movaps  xmm0,  [rsp + 000h]
    movaps  xmm1,  [rsp + 010h]
    movaps  xmm2,  [rsp + 020h]
    movaps  xmm3,  [rsp + 030h]
    movaps  xmm4,  [rsp + 040h]
    movaps  xmm5,  [rsp + 050h]
    movaps  xmm6,  [rsp + 060h]
    movaps  xmm7,  [rsp + 070h]
    movaps  xmm8,  [rsp + 080h]
    movaps  xmm9,  [rsp + 090h]
    movaps  xmm10, [rsp + 0A0h]
    movaps  xmm11, [rsp + 0B0h]
    movaps  xmm12, [rsp + 0C0h]
    movaps  xmm13, [rsp + 0D0h]
    movaps  xmm14, [rsp + 0E0h]
    movaps  xmm15, [rsp + 0F0h]
    add     rsp, 100h

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp             ; restore RBP
    add     rsp, 8          ; skip RSP placeholder slot
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax

    vmresume
    ; VMRESUME failed if we reach here — spin so we don't execute garbage.
    jmp     $

VmExitStub ENDP


; ------------------------------------------------------------
;  VmxLaunchAndEnter
;
;  Sets VMCS_GUEST_RSP = current RSP, VMCS_GUEST_RIP = VmxGuestResume,
;  then executes VMLAUNCH.
;
;  Two possible return paths:
;    a) VMLAUNCH succeeded → we are now the GUEST.
;       Execution resumes at VmxGuestResume, which returns 0 (success).
;    b) VMLAUNCH failed   → we are still the host.
;       Returns 1 (failure).
;
;  VMCS_GUEST_RIP must be pre-written to VmxGuestResume by the C++ caller
;  before calling this function (done in Vmx.cpp).
; ------------------------------------------------------------
PUBLIC VmxLaunchAndEnter

VmxLaunchAndEnter PROC
    ; Capture current RSP into VMCS_GUEST_RSP (field encoding 0x681C)
    mov     rax, 681Ch
    vmwrite rax, rsp

    vmlaunch

    ; VMLAUNCH failed.  Distinguish VMfailInvalid (CF=1) from VMfailValid (ZF=1).
    ; On success we would have jumped to VmxGuestResume and never reach here.
    jc      vmlaunch_fail_invalid   ; CF=1: no valid current VMCS
    mov     rax, 1                  ; ZF=1: VMfailValid, error code in VMCS
    ret
vmlaunch_fail_invalid:
    mov     rax, 2                  ; VMfailInvalid
    ret
VmxLaunchAndEnter ENDP


; ------------------------------------------------------------
;  VmxGuestResume — separate PROC so its address is an exported symbol.
;  The C++ caller writes &VmxGuestResume into VMCS_GUEST_RIP before
;  calling VmxLaunchAndEnter.  After a successful VMLAUNCH the guest
;  resumes here and returns 0 (success) to VmxInitCore.
; ------------------------------------------------------------
PUBLIC VmxGuestResume

VmxGuestResume PROC
    xor     rax, rax
    ret
VmxGuestResume ENDP


; ------------------------------------------------------------
;  Segment register reads  (no inline ASM in x64 MSVC)
; ------------------------------------------------------------
PUBLIC AsmGetCs
AsmGetCs PROC
    mov     ax, cs
    ret
AsmGetCs ENDP

PUBLIC AsmGetDs
AsmGetDs PROC
    mov     ax, ds
    ret
AsmGetDs ENDP

PUBLIC AsmGetEs
AsmGetEs PROC
    mov     ax, es
    ret
AsmGetEs ENDP

PUBLIC AsmGetFs
AsmGetFs PROC
    mov     ax, fs
    ret
AsmGetFs ENDP

PUBLIC AsmGetGs
AsmGetGs PROC
    mov     ax, gs
    ret
AsmGetGs ENDP

PUBLIC AsmGetSs
AsmGetSs PROC
    mov     ax, ss
    ret
AsmGetSs ENDP

PUBLIC AsmGetLdtr
AsmGetLdtr PROC
    sldt    ax
    ret
AsmGetLdtr ENDP

PUBLIC AsmGetTr
AsmGetTr PROC
    str     ax
    ret
AsmGetTr ENDP

PUBLIC AsmGetRflags
AsmGetRflags PROC
    pushfq
    pop     rax
    ret
AsmGetRflags ENDP

; AsmGetGdtr(GdtRegister* gdt)  —  RCX = pointer to { UINT16 Limit; UINT64 Base; }
PUBLIC AsmGetGdtr
AsmGetGdtr PROC
    sgdt    [rcx]
    ret
AsmGetGdtr ENDP

; AsmGetIdtr(IdtRegister* idt)
PUBLIC AsmGetIdtr
AsmGetIdtr PROC
    sidt    [rcx]
    ret
AsmGetIdtr ENDP

; AsmGetSegmentBase(UINT64 gdtBase, UINT16 selector) -> UINT64 base
;   RCX = gdtBase,  DX = selector
;   Returns 0 for null selector or system segments that need high-word extension.
PUBLIC AsmGetSegmentBase
AsmGetSegmentBase PROC
    xor     rax, rax
    movzx   rdx, dx
    test    rdx, rdx
    jz      @done           ; null selector → base 0

    ; Index into GDT: selector[15:3] * 8
    and     rdx, 0FFF8h
    add     rdx, rcx        ; rdx = &GDT[index]

    ; GDT descriptor: bytes 2-4 = base[23:0], byte 7 = base[31:24]
    movzx   rax, word ptr [rdx + 2]    ; base[15:0]
    movzx   rcx, byte ptr [rdx + 4]    ; base[23:16]
    shl     rcx, 16
    or      rax, rcx
    movzx   rcx, byte ptr [rdx + 7]    ; base[31:24]
    shl     rcx, 24
    or      rax, rcx
    ; For 64-bit system descriptors (TSS/LDT), bits [63:32] in bytes 8-11
    ; Check System bit (bit 12 of access byte at offset 5): if 0 it's a system desc
    test    byte ptr [rdx + 5], 10h
    jnz     @done           ; S=1 means code/data — base is complete in 32 bits
    ; System descriptor (e.g. TR): upper 32 bits follow in the next 8 bytes
    mov     ecx, dword ptr [rdx + 8]
    shl     rcx, 32
    or      rax, rcx
@done:
    ret
AsmGetSegmentBase ENDP


; AsmGetSegmentLimit(UINT16 selector) -> UINT32 limit
;   RCX = selector, returns byte-granular limit in EAX.
;   MSVC's __segmentlimit intrinsic is not inlined on x64 kernel builds and
;   compiles to a self-referential call that infinite-recurses.  We emit LSL
;   directly.  LSL loads the limit with granularity already applied, or leaves
;   the destination unchanged with ZF=0 on failure (invalid selector).
PUBLIC AsmGetSegmentLimit
AsmGetSegmentLimit PROC
    xor     eax, eax
    movzx   ecx, cx
    lsl     eax, ecx
    ret
AsmGetSegmentLimit ENDP


; ------------------------------------------------------------
;  AsmVmcall(UINT64 code, UINT64 arg1, UINT64 arg2, UINT64 arg3)
;
;  Windows x64: RCX=code, RDX=arg1, R8=arg2, R9=arg3
;  VmExitHandler reads the hypercall code from regs->RCX.
; ------------------------------------------------------------
PUBLIC AsmVmcall
AsmVmcall PROC
    vmcall
    ret
AsmVmcall ENDP


; ------------------------------------------------------------
;  AsmInvept(UINT64 type, UINT64* descriptor)
;
;  INVEPT invalidates cached EPT mappings.
;    type=1: single-context (flush by EPTP)
;    type=2: global         (flush everything)
;  Descriptor layout (16 bytes):
;    qword 0: EPTP
;    qword 1: reserved (0) for single-context, GPA for some variants
;
;  Windows x64: RCX = type, RDX = descriptor pointer.
; ------------------------------------------------------------
PUBLIC AsmInvept
AsmInvept PROC
    invept  rcx, oword ptr [rdx]
    ret
AsmInvept ENDP

END
