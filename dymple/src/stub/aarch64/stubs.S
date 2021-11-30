.text

.global ___stack_chk_fail
___stack_chk_fail:
	udf #0

.global dyld_stub_binder
dyld_stub_binder:
	udf #0

.data

.global ___stack_chk_guard
___stack_chk_guard:
	.quad 0
