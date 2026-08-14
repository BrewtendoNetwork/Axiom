frdu_name equ 0x00103df8
get_service_handle_len equ 0x0024b30c
get_service_handle_raw equ 0x0024a03c

discovery_call_site equ 0x157324

cave_a_start equ 0x38da88
cave_b_start equ 0x38dcb0

.org discovery_call_site
	bl discovery_picker

frd_handle       equ 0x44A9D8
nasc_environment equ 0x44A9DC

.org cave_a_start

discovery_picker:
	push    {r0, r1, r3, r4, lr}
	bl      get_nasc_environment
	mov     r4, r0

	ldr     r2, =url_fallback
	cmp     r4, #1
	ldreq   r2, =url_prod
	cmp     r4, #2
	ldreq   r2, =url_test
	cmp     r4, #3
	ldreq   r2, =url_dev

	pop     {r0, r1, r3, r4, lr}
	bx      lr

get_nasc_environment:
	push    {r4, r11, lr}

	ldr     r0, =nasc_environment
	ldr     r0, [r0]
	cmn     r0, #0
	bne     get_nasc_environment_end

	bl      get_frd_u_handle

	mrc     p15, 0x0, r4, c13, c0, 0x3
	ldr     r0, =0x00320042
	str     r0, [r4, #0x80]!
	ldr     r0, =0x70000C8
	str     r0, [r4, #0x4]
	mov     r0, 32
	str     r0, [r4, #0x8]
	ldr     r0, =frd_handle
	ldr     r0, [r0]
	swi     0x32

	mrc     p15, 0x0, r4, c13, c0, 0x3
	ldr     r0, =0x00300000
	str     r0, [r4, #0x80]!
	ldr     r0, =frd_handle
	ldr     r0, [r0]
	swi     0x32
	cmn     r0, #0
	bmi     get_nasc_environment_clear
	ldr     r2, [r4, #0x4]
	cmn     r2, #0
	bmi     get_nasc_environment_clear

	ldr     r0, [r4, #0x8]
	add     r0, r0, #1
	ldr     r1, =nasc_environment
	str     r0, [r1]

	ldr     r0, =frd_handle
	ldr     r0, [r0]
	swi     0x23

	ldr     r1, =nasc_environment
	ldr     r0, [r1]
	b       get_nasc_environment_end

get_nasc_environment_clear:
	mov     r0, #0

get_nasc_environment_end:
	pop     {r4, r11, lr}
	bx      lr

get_frd_u_handle:
	push    {r11, lr}

	ldr     r0, =frdu_name
	bl      get_service_handle_len
	mov     r2, r0

	ldr     r0, =frd_handle
	ldr     r1, =frdu_name
	mov     r3, #0
	bl      get_service_handle_raw

	pop     {r11, lr}
	bx      lr

	.pool

.org cave_b_start

url_prod:
	.asciiz "https://discovery.olv.nintendo.net/v1/endpoint"
url_test:
	.asciiz "https://discovery.olv.pretendo.cc/v1/endpoint"
url_dev:
	.asciiz "https://discovery.olv.brewtendo.org/v1/endpoint"
url_fallback:
	.asciiz "https://discovery.olv.nintendo.net/v1/endpoint"

pem_picker:
	ldr     r4, =nasc_environment
	ldr     r4, [r4]
	cmp     r4, #2
	ldreq   r4, =juxt_path
	ldrne   r4, =bver_path
	bx      lr

bver_path:
	.asciiz "3ds/bver-prod.pem"
juxt_path:
	.asciiz "3ds/juxt-prod.pem"
	.pool
