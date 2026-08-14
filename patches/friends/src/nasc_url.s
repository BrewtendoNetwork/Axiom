; NASC_URL needs to be provided at build time
; This points to the test env url
.org 0x16129a
  .area 38
    .asciiz "https://nasc.pretendo.cc/ac"
  .endarea

.org 0x1612c0
  .area 37
    .asciiz "https://nasc.dev.brewtendo.org/ac"
  .endarea
