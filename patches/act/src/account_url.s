frd_get_server_types equ 0x105340
set_base_account_url equ 0x10ed48

.org 0x10e4f8
  .area 68
    get_account_url:
      push { r0, r1, r2, r3, r4, lr }
      mov r4, r0
      mov r0, sp
      add r1, sp, #0x8
      add r2, sp, #0x4
      bl frd_get_server_types
      mov r0, sp
      ldrb r0, [r0]
      cmp r0, #0x1
      beq @set_pretendo_url
      cmp r0, #0x2
      beq @set_brewtendo_url
      @set_official_url:
        ldr r1, =official_url
        b @end
      @set_pretendo_url:
        ldr r1, =pretendo_url
        b @end
      @set_brewtendo_url:
        ldr r1, =brewtendo_url
      @end:
        mov r2, #0x0
        add r0, r4, #0x4
        bl set_base_account_url
        mov r0, #0x0
        pop { r0, r1, r2, r3, r4, pc }
      .pool
  .endarea

.org 0x125c38
  official_url:
    .asciiz "https://account.nintendo.net/v1/api/"
.org 0x125e58
  pretendo_url:
    .asciiz "https://account.pretendo.cc/v1/api/"
.org 0x125ef8
  brewtendo_url:
    .asciiz "https://account.brewtendo.org/v1/api/"
