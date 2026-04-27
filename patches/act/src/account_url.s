frd_get_server_types equ 0x105340
set_base_account_url equ 0x10ed48

.org 0x10e4f8
  .area 152
    get_account_url:
      push { r0, r1, r2, r3, r4, lr }
      mov r4, r0 ; copy url pointer to r4
      mov r0, sp
      add r1, sp, #0x8
      add r2, sp, #0x4
      bl frd_get_server_types
      mov r0, sp
      ldrb r0, [r0]
      cmp r0, #0x0
      bne @set_unofficial_url
      @set_official_url:
        adr r1, official_url
        b @end
      @set_unofficial_url:
        adr r1, unofficial_url
      @end:
        mov r2, #0x0
        add r0, r4, #0x4
        bl set_base_account_url
        mov r0, #0x0
        pop { r0, r1, r2, r3, r4, pc }
   
    ; .align will align relative to the file, not the above adr instructions
    ; This nop aligns the strings relative to the adr instructions
    ; nop

    official_url:
      .asciiz "https://account.nintendo.net/v1/api/"

    .align 4
    unofficial_url:
      .asciiz "https://account.pretendo.cc/v1/api/"
  .endarea
