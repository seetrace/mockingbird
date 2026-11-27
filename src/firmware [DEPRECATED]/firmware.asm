; Mockingbird PC/AT BIOS v0.14
[BITS 16]
org 0

RAM_MB      equ 256
KERNEL_LBA  equ 2048
CMDLINE_SEG equ 0x2000
ZP_SEG      equ 0x9000

_start:
    cli
    cld
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    mov ax, 0xF000
    mov ds, ax
    call install_bios_vectors
    call init_serial
    mov si, bios_banner
    call serial_print
    call try_direct_bzimage

    xor ax, ax
    mov es, ax
    mov dl, 0x80
    mov ax, 0x0201
    mov cx, 0x0001
    xor dh, dh
    mov bx, 0x7C00
    int 0x13
    jc disk_read_failed
    cmp word [es:0x7DFE], 0xAA55
    jne bad_boot_signature
    mov si, boot_message
    call serial_print
    mov dl, 0x80
    jmp 0x0000:0x7C00

disk_read_failed:
    mov si, disk_read_failed_message
    call serial_print
    jmp diskless
bad_boot_signature:
    mov si, bad_signature_message
    call serial_print
diskless:
    mov si, diskless_message
    call serial_print
.halt:
    sti
    hlt
    jmp .halt

try_direct_bzimage:
    call enable_a20
    call enter_unreal
    xor ax, ax
    mov es, ax
    mov ax, ZP_SEG
    mov fs, ax

    mov edi, 0x90000
    mov eax, KERNEL_LBA
    mov ecx, 2
    call ata_read
    jc .read_failed
    mov si, dap_read_message
    call serial_print

    cmp dword [fs:0x202], 0x53726448        ; "HdrS"
    jne .fallback
    mov si, hdrs_found_message
    call serial_print

    movzx ebx, byte [fs:0x1F1]
    test bl, bl
    jnz .have_setup
    mov bl, 4
.have_setup:
    inc ebx
    mov ecx, [fs:0x1F4]
    add ecx, 31
    shr ecx, 5

    mov ax, ZP_SEG                           ; zero everything except header 0x1F1..0x26F
    mov es, ax
    xor eax, eax
    xor di, di
    push ecx
    mov cx, 0x1F0 / 4
    rep stosd
    mov di, 0x270
    mov cx, (0x1000 - 0x270) / 4
    rep stosd
    pop ecx
    xor ax, ax
    mov es, ax

    mov eax, KERNEL_LBA
    add eax, ebx
    mov edi, 0x100000
    call ata_read
    jc .payload_failed
    mov si, payload_loaded_message
    call serial_print

    mov ax, CMDLINE_SEG
    mov es, ax
    xor di, di
    mov si, cmdline
.cp:
    lodsb
    stosb
    test al, al
    jnz .cp
    xor ax, ax
    mov es, ax

    mov byte [fs:0x210], 0xFF
    or  byte [fs:0x211], 0x01
    mov dword [fs:0x228], CMDLINE_SEG * 16

    mov byte [fs:0x1E8], 3                   ; e820
    mov dword [fs:0x2D0], 0
    mov dword [fs:0x2D4], 0
    mov dword [fs:0x2D8], 0x9F000
    mov dword [fs:0x2DC], 0
    mov dword [fs:0x2E0], 1
    mov dword [fs:0x2E4], 0x9F000
    mov dword [fs:0x2E8], 0
    mov dword [fs:0x2EC], 0x61000
    mov dword [fs:0x2F0], 0
    mov dword [fs:0x2F4], 2
    mov dword [fs:0x2F8], 0x100000
    mov dword [fs:0x2FC], 0
    mov dword [fs:0x300], RAM_MB * 0x100000 - 0x100000
    mov dword [fs:0x304], 0
    mov dword [fs:0x308], 1

    mov si, direct_boot_message
    call serial_print
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp dword 0x08:(0xF0000 + pm_entry)

.fallback:
    ret
.payload_failed:
    mov si, payload_failed_message
    call serial_print
    ret
.read_failed:
    mov si, dap_failed_message
    call serial_print
    ret

enable_a20:
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    ret

enter_unreal:
    lgdt [cs:gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short $+2
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp 0xF000:.flush
.flush:
    xor ax, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ax, 0xF000
    mov ds, ax
    ret

; eax=LBA28, ecx=count, es:edi=dest. CF=1 on error.
ata_read:
    test ecx, ecx
    jz .ok
.next:
    push eax
    push ecx
    mov ebx, eax
    mov dx, 0x1F2
    mov al, 1
    out dx, al
    mov dx, 0x1F3
    mov al, bl
    out dx, al
    shr ebx, 8
    mov dx, 0x1F4
    mov al, bl
    out dx, al
    shr ebx, 8
    mov dx, 0x1F5
    mov al, bl
    out dx, al
    shr ebx, 8
    and bl, 0x0F
    or bl, 0xE0
    mov dx, 0x1F6
    mov al, bl
    out dx, al
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al
    mov esi, 0x2000000
.poll:
    in al, dx
    test al, 0x80
    jnz .busy
    test al, 0x08
    jnz .ready
    test al, 0x01
    jnz .err
.busy:
    dec esi
    jnz .poll
.err:
    pop ecx
    pop eax
    stc
    ret
.ready:
    mov dx, 0x1F0
    mov cx, 256
    a32 rep insw
    pop ecx
    pop eax
    inc eax
    dec ecx
    jnz .next
.ok:
    clc
    ret

install_bios_vectors:
    xor ax, ax
    mov es, ax
    mov word [es:0x10*4], int10_handler
    mov word [es:0x10*4+2], 0xF000
    mov word [es:0x13*4], int13_handler
    mov word [es:0x13*4+2], 0xF000
    mov word [es:0x16*4], int16_handler
    mov word [es:0x16*4+2], 0xF000
    mov word [es:0x18*4], int18_handler
    mov word [es:0x18*4+2], 0xF000
    mov word [es:0x19*4], int19_handler
    mov word [es:0x19*4+2], 0xF000
    ret

init_serial:                                 ; 115200 8N1, FIFO on
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x03
    out dx, al
    ret

serial_putc:
    push ax
    push cx
    push dx
    mov ah, al
    mov dx, 0x3FD
    mov cx, 0xFFFF
.w: in al, dx
    test al, 0x20
    jnz .go
    loop .w
.go:
    mov dx, 0x3F8
    mov al, ah
    out dx, al
    pop dx
    pop cx
    pop ax
    ret

serial_print:
    push ax
.l: lodsb
    test al, al
    jz .d
    call serial_putc
    jmp .l
.d: pop ax
    ret

int10_handler:
    cmp ah, 0x0E
    je .tty
    cmp ah, 0x03
    je .cur
    xor ah, ah
    iret
.tty:
    call serial_putc
    iret
.cur:
    xor dx, dx
    mov cx, 0x0607
    iret

int16_handler:                               ; buffer byte at 0040:00F0
    push ds
    push dx
    push bx
    mov bx, 0x40
    mov ds, bx
    cmp ah, 0x01
    je .status
    test ah, ah
    jnz .none
.read:
    mov al, [0xF0]
    mov byte [0xF0], 0
    test al, al
    jnz .got
    mov dx, 0x3FD
.rw:
    in al, dx
    test al, 1
    jz .rw
    mov dx, 0x3F8
    in al, dx
.got:
    xor ah, ah
    pop bx
    pop dx
    pop ds
    iret
.status:
    cmp byte [0xF0], 0
    jne .have
    mov dx, 0x3FD
    in al, dx
    test al, 1
    jz .none
    mov dx, 0x3F8
    in al, dx
    mov [0xF0], al
.have:
    mov al, [0xF0]
    xor ah, ah
    pop bx
    pop dx
    pop ds
    push bp
    mov bp, sp
    and word [bp+8], 0xFFBF
    pop bp
    iret
.none:
    xor ax, ax
    pop bx
    pop dx
    pop ds
    push bp
    mov bp, sp
    or word [bp+8], 0x0040
    pop bp
    iret

; INT 13h, read-only. Returns with retf 2 so CF reaches the caller.
int13_handler:
    cmp dl, 0x80
    jne .bad
    cmp ah, 0x00
    je .ok
    cmp ah, 0x02
    je .chs_read
    cmp ah, 0x08
    je .geom
    cmp ah, 0x41
    je .edd
    cmp ah, 0x42
    je .lba_read
.bad:
    mov ah, 0x01
    sti
    stc
    retf 2
.ok:
    xor ah, ah
    sti
    clc
    retf 2
.edd:
    mov bx, 0x55AA
    mov ax, 0x3000
    mov cx, 0x0001
    sti
    clc
    retf 2
.geom:
    xor ax, ax
    xor bl, bl
    mov ch, 0xFF
    mov cl, 0x20
    mov dh, 7
    mov dl, 1
    sti
    clc
    retf 2

.lba_read:
    push eax
    push ecx
    push edi
    push ebx
    movzx ecx, word [si+2]
    movzx edi, word [si+6]
    shl edi, 4
    movzx eax, word [si+4]
    add edi, eax
    mov eax, [si+8]
    jmp .do_read

.chs_read:
    push eax
    push ecx
    push edi
    push ebx
    movzx ebx, al
    movzx edi, cl
    and edi, 0x3F
    dec edi
    mov al, cl
    shr al, 6
    movzx eax, al
    shl eax, 8
    movzx ecx, ch
    or  ecx, eax
    shl ecx, 3
    movzx eax, dh
    add ecx, eax
    shl ecx, 5
    add ecx, edi
    mov eax, ecx
    mov ecx, ebx
    movzx edi, word [esp]
    mov bx, es
    movzx ebx, bx
    shl ebx, 4
    add edi, ebx

.do_read:
    push es
    push ds
    push esi
    push dx
    mov bx, 0xF000
    mov ds, bx
    xor bx, bx
    mov es, bx
    call ata_read
    pop dx
    pop esi
    pop ds
    pop es
    pop ebx
    pop edi
    pop ecx
    pop eax
    jc .fail
    xor ah, ah
    sti
    clc
    retf 2
.fail:
    mov ah, 0x04
    sti
    stc
    retf 2

int18_handler:
    push ds
    push si
    mov ax, 0xF000
    mov ds, ax
    mov si, diskless_message
    call serial_print
    pop si
    pop ds
    iret

int19_handler:
    jmp 0xF000:_start

bios_banner:
    db "Mockingbird PC/AT BIOS v0.14", 13, 10
    db "GPL 3.0, GNU General Public Licence", 13, 10
    db "Made by Yuta Seiya!", 13, 10, 0
boot_message:            db "Bootable disk found, starting MBR...", 13, 10, 0
direct_boot_message:     db "Jumping to Linux protected-mode entry.", 13, 10, 0
dap_read_message:        db "Direct ATA read succeeded.", 13, 10, 0
dap_failed_message:      db "Direct ATA read failed.", 13, 10, 0
hdrs_found_message:      db "Linux HdrS setup header found.", 13, 10, 0
payload_loaded_message:  db "Linux kernel payload loaded at 1 MiB.", 13, 10, 0
payload_failed_message:  db "Linux kernel payload read failed.", 13, 10, 0
disk_read_failed_message: db "BIOS disk read failed.", 13, 10, 0
bad_signature_message:   db "Boot sector signature missing.", 13, 10, 0
diskless_message:        db "Non-system disk or disk error.", 13, 10, 0
cmdline:
    db "console=ttyS0,115200 earlyprintk=serial,ttyS0,115200 noapic nolapic acpi=off", 0

align 8
gdt_start:
    dq 0
    dw 0xFFFF, 0
    db 0, 10011010b, 11001111b, 0            ; 0x08 code32 flat
    dw 0xFFFF, 0
    db 0, 10010010b, 11001111b, 0            ; 0x10 data flat
gdt_end:
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd 0xF0000 + gdt_start

[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    cld
    xor ebx, ebx
    xor ebp, ebp
    xor edi, edi
    mov esi, 0x90000
    mov eax, 0x100000
    jmp eax

[BITS 16]
times 0xFFF0 - ($ - $$) db 0xFF
reset_vector:
    jmp 0xF000:0x0000
times 0x10000 - ($ - $$) db 0xFF