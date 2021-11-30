.text

.global _dymple_bind_stub_raw
_dymple_bind_stub_raw:
	udf #0

.global ___stack_chk_fail
___stack_chk_fail:
	udf #0

.data
.global ___stack_chk_guard
___stack_chk_guard:
	.long 0
