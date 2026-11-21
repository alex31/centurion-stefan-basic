; Checked DEFINT arithmetic. Update *value only on success and return 1.
; At entry the saved caller X is at (s), then the pointer and the right operand.
	.setcpu 6
	.code
	.export _ib_add16
	.export _ib_sub16
	.export _ib_mul16

; CPU6 function _ib_add16 begin
_ib_add16:
	lda 2(s)
	ldb 4(s)
	add 0(a),b
	bf add_range
	stb (a)
	clr b,1
	rsr
add_range:
	clr b
	rsr
; CPU6 function _ib_add16 end

; CPU6 function _ib_sub16 begin
_ib_sub16:
	lda 2(s)
	ldb 4(s)
	sub 0(a),b
	bf sub_range
	stb (a)
	clr b,1
	rsr
sub_range:
	clr b
	rsr
; CPU6 function _ib_sub16 end

; CPU6 function _ib_mul16 begin
_ib_mul16:
	stx (-s)
	ldx 4(s)
	lda (x)
	ldb 6(s)
	mul b,a
	stb (-s)
	; Correct the high half of the unsigned product for each negative operand.
	ldb (x)
	bp mul_right
	ldb 8(s)
	sub a,b
	xab
mul_right:
	ldb 8(s)
	bp mul_check
	ldb (x)
	sub a,b
	xab
mul_check:
	ldb (s+)
	bp mul_high
	ina
mul_high:
	ori a,a
	bnz mul_range
	stb (x)
	ldx (s+)
	clr b,1
	rsr
mul_range:
	ldx (s+)
	clr b
	rsr
; CPU6 function _ib_mul16 end
