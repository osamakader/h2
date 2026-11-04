/* The guest's ELF file, embedded by make GUEST=...; empty without one. */
.section .rodata.guest, "a"
.balign 16
.global hv_guest_image
.global hv_guest_image_end
hv_guest_image:
#ifdef HV_GUEST_IMAGE
	.incbin HV_GUEST_IMAGE
#endif
hv_guest_image_end:

/* a second guest, make GUEST2=...: empty without one */
.balign 16
.global hv_guest2_image
.global hv_guest2_image_end
hv_guest2_image:
#ifdef HV_GUEST2_IMAGE
	.incbin HV_GUEST2_IMAGE
#endif
hv_guest2_image_end:
