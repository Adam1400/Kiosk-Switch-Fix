/**
 * @file svc.s
 * @copyright libnx Authors
 */

.macro SVC_BEGIN name
	.section .text.\name, "ax", %progbits
	.global \name
	.type \name, %function
	.align 2
	.cfi_startproc
\name:
.endm

.macro SVC_END
	.cfi_endproc
.endm

SVC_BEGIN svcQueryIoMapping
	STR X0, [SP, #-16]!
	SVC 0x55
	LDR X2, [SP], #16
	STR X1, [X2]
	RET
SVC_END

SVC_BEGIN svcAttachDeviceAddressSpace
	SVC 0x57
	RET
SVC_END

SVC_BEGIN svcQueryMemory
	STR X1, [SP, #-16]!
	SVC 0x6
	LDR X2, [SP], #16
	STR W1, [X2]
	RET
SVC_END

SVC_BEGIN svcSetMemoryPermission
	SVC 0x2
	RET
SVC_END

SVC_BEGIN svcSetProcessMemoryPermission
	SVC 0x73
	RET
SVC_END

SVC_BEGIN svcCallSecureMonitor
	STR X0, [SP, #-16]!
	MOV X8, X0
	LDP X0, X1, [X8]
	LDP X2, X3, [X8, #0x10]
	LDP X4, X5, [X8, #0x20]
	LDP X6, X7, [X8, #0x30]
	SVC 0x7F
	LDR X8, [SP], #16
	STP X0, X1, [X8]
	STP X2, X3, [X8, #0x10]
	STP X4, X5, [X8, #0x20]
	STP X6, X7, [X8, #0x30]
	RET
SVC_END