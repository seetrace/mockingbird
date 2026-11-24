[BITS 16]

[SECTION .text]

global _start

_start:
    ; Setup segments
    cli

    mov ax, cs
    mov ds, ax

    ; Setup stack
    mov ax, 0x7000
    mov ss, ax
    mov sp, 0xFFFE

    ; Print firmware startup message
    mov si, message - $$
    call serial_print

    ; Halt for now
.halt:
    jmp .halt


; --------------------------------
; Serial output
; --------------------------------

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


; --------------------------------
; Firmware strings
; --------------------------------

message:
    db "Hello from guest BIOS!", 13, 10
    db "Hello from Mockingbird BIOS v0.12", 13, 10
    db 0


; --------------------------------
; BIOS reset vector
; --------------------------------

[SECTION .reset]

global reset_vector

reset_vector:
    jmp 0xF000:0x0000
