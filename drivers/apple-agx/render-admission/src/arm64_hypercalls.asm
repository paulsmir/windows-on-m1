        AREA |.text|, CODE, READONLY, ALIGN=2
        EXPORT AdmissionHvcArmConsumed
        EXPORT AdmissionHvcGuestIpaPa

; Leaf calls preserve SP/LR and all nonvolatile registers.  The hypervisor
; receives the C argument in X0 and returns the private status in X0.
AdmissionHvcArmConsumed PROC
        hvc #0x4d32
        ret
        ENDP

AdmissionHvcGuestIpaPa PROC
        hvc #0x4d31
        ret
        ENDP
        END
