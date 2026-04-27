; For some reason I'm unable to get armips' thumb adr to work
; so this will fill the gap
.macro adr,reg,loc
  add reg, pc, loc - org() - 2 ; - 2 for the instruction
  .endmacro
