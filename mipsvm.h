#ifndef __MIPSVM_H__
#define __MIPSVM_H__
// public

typedef enum
{
    MIPSVM_RC_OK,
    MIPSVM_RC_RESERVED_INSTR,
    MIPSVM_RC_WRITE_ADDRESS_ERROR,
    MIPSVM_RC_READ_ADDRESS_ERROR,
    MIPSVM_RC_INTEGER_OVERFLOW,
    MIPSVM_RC_BREAK,
    MIPSVM_RC_SYSCALL,
    MIPSVM_RC_TRAP,
} mipsvm_rc_t;

typedef struct
{
    uint32_t (*readw)(uint32_t addr);
    uint8_t (*readb)(uint32_t addr);
    uint16_t (*readh)(uint32_t addr);
    void (*writew)(uint32_t addr, uint32_t data);
    void (*writeb)(uint32_t addr, uint8_t data);
    void (*writeh)(uint32_t addr, uint16_t data);
} mipsvm_iface_t;

typedef struct
{
    mipsvm_iface_t iface;
    uint32_t pc;
    uint32_t branch_pc;
    uint32_t code;
    int branch_is_pending;
    mipsvm_rc_t exception;
    union
    {
        uint64_t acc;
        struct
        {
            uint32_t lo;
            uint32_t hi;
        };
    };
    uint32_t gpr[32];
} mipsvm_t;

void mipsvm_init(mipsvm_t *ctx, const mipsvm_iface_t *iface, uint32_t reset_pc);
mipsvm_rc_t mipsvm_exec(mipsvm_t *ctx);
uint32_t mipsvm_get_callcode(const mipsvm_t *ctx);

#endif
