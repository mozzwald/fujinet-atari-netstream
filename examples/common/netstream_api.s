; cc65/ca65 wrappers for NETStream engine jump table

		.export _ns_begin_stream
		.export _ns_end_stream
		.export _ns_get_version
		.export _ns_get_base
		.export _ns_send_byte
		.export _ns_recv_byte
		.export _ns_bytes_avail
		.export _ns_get_status
		.export _ns_get_video_std
		.export _ns_init_netstream
		.export _ns_get_final_flags
		.export _ns_get_final_audf3
		.export _ns_get_final_audf4

NS_BASE = $2800

_ns_begin_stream:
		jsr		NS_BASE+0
		rts

_ns_end_stream:
		jsr		NS_BASE+3
		rts

_ns_get_version:
		jsr		NS_BASE+6
		ldx		#0
		rts

_ns_get_base:
		jsr		NS_BASE+9
		rts

; Input: A = byte, Output: A=0 ok, A=1 full
_ns_send_byte:
		jsr		NS_BASE+12
		bcc		ok_send
		lda		#1
		rts
ok_send:
		lda		#0
		rts

; Output: A/X = byte (0-255), or $FFFF if empty
_ns_recv_byte:
		jsr		NS_BASE+15
		bcc		ok_recv
		lda		#$ff
		ldx		#$ff
		rts
ok_recv:
		ldx		#0
		rts

_ns_bytes_avail:
		jsr		NS_BASE+18
		rts

_ns_get_status:
		jsr		NS_BASE+21
		ldx		#0
		rts

_ns_get_video_std:
		jsr		NS_BASE+24
		ldx		#0
		rts

_ns_init_netstream:
		jmp		NS_BASE+27

_ns_get_final_flags:
		jsr		NS_BASE+30
		ldx		#0
		rts

_ns_get_final_audf3:
		jsr		NS_BASE+33
		ldx		#0
		rts

_ns_get_final_audf4:
		jsr		NS_BASE+36
		ldx		#0
		rts
