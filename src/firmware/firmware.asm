[BITS 16]

[SECTION .text]

global _start

_start:
    cli
    xor ax, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE

    mov ax, 0xF000
    mov ds, ax

    call install_bios_vectors
    call init_serial

    mov si, bios_banner - $$
    call serial_print

    call try_direct_bzimage

    mov dl, 0x80
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 1
    mov dh, 0
    xor bx, bx
    mov es, bx
    mov bx, 0x7C00
    int 0x13
    jc disk_read_failed

    cmp word [es:0x7DFE], 0xAA55
    jne bad_boot_signature

    mov si, boot_message - $$
    call serial_print
    jmp 0x0000:0x7C00

diskless:
    mov si, diskless_message - $$
    call serial_print
.halt:
    sti
    hlt
    jmp .halt


try_direct_bzimage:
    push es
    push ds

    mov ax, 0x9000
    mov es, ax
    xor bx, bx

    mov si, kernel_dap_setup
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc .read_failed

    mov si, dap_read_message - $$
    mov ax, 0xF000
    mov ds, ax
    call serial_print
    mov ax, 0xF000
    mov es, ax

    mov ax, 0x9000
    mov es, ax

    cmp dword [es:0x202], 0x53726448
    jne .fallback

    mov si, hdrs_found_message - $$
    mov ax, 0xF000
    mov ds, ax
    call serial_print

    mov si, payload_loaded_message - $$
    mov ax, 0xF000
    mov ds, ax
    call serial_print

    pop ds
    pop es

    mov ax, 0x9000
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE

    mov byte [0x210], 0xFF
    mov byte [0x211], 0
    mov dword [0x228], 0x00100000
    mov dword [0x230], 0
    mov dword [0x234], 0

    mov si, direct_boot_message - $$
    mov ax, 0xF000
    mov ds, ax
    call serial_print

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_start

.fallback:
    pop ds
    pop es
    ret

.payload_failed:
    mov si, payload_failed_message - $$
    mov ax, 0xF000
    mov ds, ax
    call serial_print
    jmp .fallback

.read_failed:
    mov si, dap_failed_message - $$
    mov ax, 0xF000
    mov ds, ax
    call serial_print
    jmp .fallback

disk_read_failed:
    mov si, disk_read_failed_message - $$
    call serial_print
    jmp diskless

bad_boot_signature:
    mov si, bad_signature_message - $$
    call serial_print
    jmp diskless


install_bios_vectors:
    xor ax, ax
    mov es, ax

    mov word [es:0x10 * 4], int10_handler
    mov word [es:(0x10 * 4) + 2], 0xF000

    mov word [es:0x13 * 4], int13_handler
    mov word [es:(0x13 * 4) + 2], 0xF000

    mov word [es:0x16 * 4], int16_handler
    mov word [es:(0x16 * 4) + 2], 0xF000

    mov word [es:0x18 * 4], int18_handler
    mov word [es:(0x18 * 4) + 2], 0xF000

    mov word [es:0x19 * 4], int19_handler
    mov word [es:(0x19 * 4) + 2], 0xF000
    ret


; Configure COM1 for 3,072 baud if the host serial backend is connected.
init_serial:
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x0C
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FC
    mov al, 0x03
    out dx, al
    mov dx, 0x3F9
    mov al, 0x01
    out dx, al
    ret


; INT 10h - Video BIOS services. Output is mapped to COM1.
int10_handler:
    cmp ah, 0x00
    je .set_mode
    cmp ah, 0x03
    je .get_cursor
    cmp ah, 0x0E
    je .teletype
    jmp .unsupported

.set_mode:
    xor ah, ah
    iret

.get_cursor:
    xor dx, dx
    xor cx, cx
    mov cl, 0x07
    xor bh, bh
    iret

.teletype:
    mov dx, 0x3F8
    out dx, al
    iret

.unsupported:
    mov ah, 0x01
    iret


; INT 16h - Keyboard BIOS services. Input is mapped to COM1.
int16_handler:
    cmp ah, 0x00
    je .read
    cmp ah, 0x01
    je .status
    jmp .unsupported

.read:
    mov dx, 0x3FD
.read_wait:
    in al, dx
    test al, 0x01
    jz .read_wait
    mov dx, 0x3F8
    in al, dx
    xor ah, ah
    iret

.status:
    mov dx, 0x3FD
    in al, dx
    test al, 0x01
    jz .none
    mov dx, 0x3F8
    in al, dx
    xor ah, ah
    and eax, 0xFFFF
    iret

.none:
    push ax
    mov ax, 0
    pushf
    pop ax
    pop ax
    mov al, 0
    mov ah, 0
    iret

.unsupported:
    mov ah, 0x01
    iret


; INT 13h - BIOS Disk Services for the primary fixed disk.
int13_handler:
    cmp dl, 0x80
    jne .unsupported

    cmp ah, 0x00
    je disk_reset
    cmp ah, 0x41
    je edd_install_check
    cmp ah, 0x42
    je disk_read_lba
    cmp ah, 0x48
    je edd_parameters
    cmp ah, 0x02
    je disk_read
    cmp ah, 0x03
    je disk_write
    cmp ah, 0x08
    je disk_geometry

.unsupported:
    mov ah, 0x01
    stc
    iret

disk_reset:
    mov ah, 0
    clc
    iret

edd_install_check:
    mov bx, 0x55AA
    mov ah, 0x30
    mov al, 0
    mov cx, 0x0001
    mov dl, 0x80
    clc
    iret

edd_parameters:
    push di
    mov di, si
    mov word [ds:di], 30
    mov word [ds:di + 2], 0
    mov dword [ds:di + 12], 131072
    mov dword [ds:di + 16], 512
    mov word [ds:di + 20], 0
    mov word [ds:di + 22], 8
    mov word [ds:di + 24], 32
    pop di
    mov ah, 0
    clc
    iret

disk_read_lba:
    push ds
    push si
    push di
    push es
    push bx

    mov ax, ds
    mov es, ax

    mov di, si
    mov al, [ds:si + 2]
    mov byte [disk_sector_count], al
    mov bx, [ds:si + 4]
    mov ax, [ds:si + 6]
    mov es, ax

    mov eax, [ds:si + 8]
    mov [disk_lba], eax

.sector:
    call ata_setup_read
    call ata_wait_drq
    jc disk_read_lba_error

    mov di, bx
    mov cx, 256
    mov dx, 0x1F0
    rep insw

    add bx, 512
    jnc .next
    mov ax, es
    add ax, 0x20
    mov es, ax
.next:
    call ata_increment_lba
    dec byte [disk_sector_count]
    mov al, [disk_sector_count]
    test al, al
    jne .sector

    pop bx
    pop es
    pop di
    pop si
    pop ds
    mov ah, 0
    mov al, 1
    clc
    iret

disk_read_lba_error:
    pop bx
    pop es
    pop di
    pop si
    pop ds
    mov ah, 0x04
    mov al, 0
    stc
    iret

disk_geometry:
    mov ah, 0
    mov al, 0
    mov bl, 0
    mov ch, 0xFF
    mov cl, 0x20
    mov dh, 0x07
    mov dl, 0x81
    clc
    iret

disk_read:
    push es
    push bx
    mov byte [disk_sector_count], al
    call chs_to_lba
    mov cl, [disk_sector_count]
    test cl, cl
    jz disk_read_done

.sector:
    call ata_setup_read
    call ata_wait_drq
    jc disk_read_error

    mov di, bx
    mov cx, 256
    mov dx, 0x1F0
    rep insw

    add bx, 512
    jc .next_segment
    jmp .sector_advance
.next_segment:
    mov ax, es
    add ax, 0x20
    mov es, ax
.sector_advance:
    call ata_increment_lba
    dec byte [disk_sector_count]
    mov cl, [disk_sector_count]
    cmp cl, 0
    jne .sector

disk_read_done:
    pop bx
    pop es
    mov ah, 0
    mov al, 1
    clc
    iret

disk_read_error:
    pop bx
    pop es
    mov ah, 0x04
    mov al, 0
    stc
    iret

disk_write:
    push ds
    push es
    push bx
    mov byte [disk_sector_count], al
    call chs_to_lba
    mov cl, [disk_sector_count]
    test cl, cl
    jz disk_write_done

.sector:
    call ata_setup_write
    call ata_wait_drq
    jc disk_write_error

    push es
    pop ds
    mov si, bx
    mov cx, 256
    mov dx, 0x1F0
    rep outsw
    xor ax, ax
    mov ds, ax

    add bx, 512
    jnc .sector_advance
    mov ax, es
    add ax, 0x20
    mov es, ax
.sector_advance:
    call ata_increment_lba
    dec byte [disk_sector_count]
    mov cl, [disk_sector_count]
    cmp cl, 0
    jne .sector

disk_write_done:
    pop bx
    pop es
    pop ds
    mov ah, 0
    mov al, 1
    clc
    iret

disk_write_error:
    pop bx
    pop es
    pop ds
    mov ah, 0x05
    mov al, 0
    stc
    iret


chs_to_lba:
    ; Convert 8-head/32-sector superfloppy CHS to LBA.
    push bx
    xor ax, ax
    mov al, ch
    shl ax, 2
    mov bl, cl
    shr bl, 6
    xor bh, bh
    add ax, bx

    mov bx, 256
    mul bx
    mov word [disk_lba], ax
    mov word [disk_lba + 2], dx

    xor ax, ax
    mov al, dh
    mov bx, 32
    mul bx
    add word [disk_lba], ax
    adc word [disk_lba + 2], dx

    mov al, cl
    and al, 31
    dec al
    xor ah, ah
    add word [disk_lba], ax
    adc word [disk_lba + 2], 0
    pop bx
    ret


ata_setup_read:
    mov dx, 0x1F2
    mov al, 1
    out dx, al
    call ata_setup_lba
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al
    ret

ata_setup_write:
    mov dx, 0x1F2
    mov al, 1
    out dx, al
    call ata_setup_lba
    mov dx, 0x1F7
    mov al, 0x30
    out dx, al
    ret

ata_setup_lba:
    mov dx, 0x1F3
    mov al, [disk_lba]
    out dx, al
    mov dx, 0x1F4
    mov al, [disk_lba + 1]
    out dx, al
    mov dx, 0x1F5
    mov al, [disk_lba + 2]
    out dx, al
    mov dx, 0x1F6
    mov al, [disk_lba + 3]
    and al, 0x0F
    or al, 0xE0
    out dx, al

    ret


ata_wait_drq:
    push cx
    mov cx, 65535
.poll:
    mov dx, 0x1F7
    in al, dx
    test al, 0x80
    jnz .next
    test al, 0x08
    jnz .ready
    test al, 0x01
    jnz .error
.next:
    loop .poll
    pop cx
    stc
    ret
.error:
    pop cx
    stc
    ret
.ready:
    pop cx
    clc
    ret


ata_increment_lba:
    add word [disk_lba], 1
    adc word [disk_lba + 2], 0
    ret


int16_status_with_data:
    ret


int18_handler:
    mov si, diskless_message - $$
    call serial_print
    iret

int19_handler:
    jmp _start


serial_print:
    mov dx, 0x3F8
.loop:
    lodsb
    test al, al
    jz .done
    out dx, al
    jmp .loop
.done:
    ret


bios_banner:
    db "Mockingbird PC/AT BIOS v0.13", 13, 10
    db "GPL 3.0, GNU General Public Licence", 13, 10
    db "Made by Yuta Seiya!", 13, 10
    db 0

boot_message:
    db "Booteble Disk Found!", 13, 10
    db "Loading disk MBR into memory", 13, 10
    db "Starting...", 13, 10
    db 0

direct_boot_message:
    db "Direct bzImage setup found.", 13, 10
    db 0

gdt_start:
    dq 0
    dw 0xFFFF
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start + 0xF0000

gdt_end:

[BITS 32]
protected_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x00100000
    mov esi, 0x00090000
    jmp 0x100000

dap_read_message:
    db "Direct DAP read succeeded.", 13, 10
    db 0

dap_failed_message:
    db "Direct DAP read failed.", 13, 10
    db 0

hdrs_found_message:
    db "Linux HdrS setup header found.", 13, 10
    db 0

payload_loaded_message:
    db "Linux kernel payload loaded.", 13, 10
    db 0

payload_failed_message:
    db "Linux kernel payload read failed.", 13, 10
    db 0

disk_read_failed_message:
    db "BIOS disk read failed.", 13, 10
    db 0

bad_signature_message:
    db "Boot sector signature missing.", 13, 10
    db 0

diskless_message:
    db "Non-system disk or disk error.", 13, 10
    db "Press Ctl + C to exit", 13, 10
    db 0

disk_sector_count:
    db 0

disk_lba:
    dd 0

kernel_dap_setup:
    db 16
    db 0
    dw 32
    dw 0
    dw 0x9000
    dd 2048
    dd 0

[SECTION .reset]

[BITS 16]

global reset_vector

reset_vector:
    jmp 0xF000:0x0000
