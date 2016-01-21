#define __MIPSVM_C__

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "mipsvm.h"

static void schedule_abs_branch(mipsvm_t *ctx, uint32_t dst)
{
    ctx->branch_pc = dst;
    ctx->branch_is_pending = 1;
}

static void schedule_rel_branch(mipsvm_t *ctx, int32_t offset)
{
    ctx->branch_pc = ctx->pc + offset;
    ctx->branch_is_pending = 1;
}

static uint8_t readb(mipsvm_t *ctx, uint32_t addr)
{
    return ctx->iface.readb(addr);
}

static uint16_t readh(mipsvm_t *ctx, uint32_t addr)
{
    if (addr % 2)
    {
        ctx->exception = MIPSVM_RC_READ_ADDRESS_ERROR;
        return 0;
    }
    return ctx->iface.readh(addr);
}

static uint32_t readw(mipsvm_t *ctx, uint32_t addr)
{
    if (addr % 4)
    {
        ctx->exception = MIPSVM_RC_READ_ADDRESS_ERROR;
        return 0;
    }

    return ctx->iface.readw(addr);
}

static void writeb(mipsvm_t *ctx, uint32_t addr, uint8_t data)
{
    ctx->iface.writeb(addr, data);
}

static void writeh(mipsvm_t *ctx, uint32_t addr, uint16_t data)
{
    if (addr % 2)
    {
        ctx->exception = MIPSVM_RC_WRITE_ADDRESS_ERROR;
        return;
    }

    ctx->iface.writeh(addr, data);
}

static void writew(mipsvm_t *ctx, uint32_t addr, uint32_t data)
{
    if (addr % 4)
    {
        ctx->exception = MIPSVM_RC_WRITE_ADDRESS_ERROR;
        return;
    }

    ctx->iface.writew(addr, data);
}

static bool exec_special(mipsvm_t *ctx, uint32_t instr)
{
    const int func = instr & 0x3F;
    const int rs = (instr >> 21) & 0x1F;
    const int rt = (instr >> 16) & 0x1F;
    const int rd = (instr >> 11) & 0x1F;
    const int aux = (instr >> 6) & 0x1F;

    if (rs == 0 && rt == 0 && aux == 0) // rs, rt, aux unused
    {
        switch (func)
        {
        case 0x10:              // mfhi
            ctx->gpr[rd] = ctx->hi;
            return 1;

        case 0x12:              // mflo
            ctx->gpr[rd] = ctx->lo;
            return 1;
        }
    }

    if (rt == 0 && rd == 0 && aux == 0)  // aux, rd, rt unused
    {
        switch (func)
        {
        case 0x11:  // mthi
            ctx->hi = ctx->gpr[rs];
            return 1;

        case 0x13:  // mtlo
            ctx->lo = ctx->gpr[rs];
            return 1;
        }
    }

    if (rt == 0 && rd == 0) // rt, rd are unused
    {
        switch (func)
        {
        case 0x08:  // jr
            schedule_abs_branch(ctx, ctx->gpr[rs]);
            return 1;
        }
    }

    if (rd == 0 && aux == 0)    // rd, aux unused
    {
        switch (func)
        {
        case 0x1A:  // div
            ctx->lo = (int32_t) ctx->gpr[rs] / (int32_t) ctx->gpr[rt];
            ctx->hi = (int32_t) ctx->gpr[rs] % (int32_t) ctx->gpr[rt];
            return 1;

        case 0x1B:  // divu
            ctx->lo = ctx->gpr[rs] / ctx->gpr[rt];
            ctx->hi = ctx->gpr[rs] % ctx->gpr[rt];
            return 1;

        case 0x18:  // mult
            ctx->acc = ((int64_t) (int32_t) ctx->gpr[rs]) * (int32_t) ctx->gpr[rt];
            return 1;

        case 0x19:  // multu
            ctx->acc = (uint64_t) ctx->gpr[rs] * ctx->gpr[rt];
            return 1;
        }
    }

    if (aux == 0)  // aux unused
    {
        switch (func)
        {
        case 0x0A:  // movz
            if (ctx->gpr[rt] == 0)
                ctx->gpr[rd] = ctx->gpr[rs];
            return 1;

        case 0x0B:  // movn
            if (ctx->gpr[rt] != 0)
                ctx->gpr[rd] = ctx->gpr[rs];
            return 1;

        case 0x2A:  // slt
            ctx->gpr[rd] = (int32_t)ctx->gpr[rs] < (int32_t)ctx->gpr[rt];
            return 1;

        case 0x2B:  // sltu
            ctx->gpr[rd] = ctx->gpr[rs] < ctx->gpr[rt];
            return 1;

        case 0x20:  // add (w overflow)
            {
                uint32_t tmp = ctx->gpr[rs] + ctx->gpr[rt];
                if (((tmp ^ ctx->gpr[rs]) & (tmp ^ ctx->gpr[rt])) >> 31)
                    ctx->exception = MIPSVM_RC_INTEGER_OVERFLOW;
                else
                    ctx->gpr[rd] = tmp;
            }
            return 1;

        case 0x21:  // addu (wo overflow)
            ctx->gpr[rd] = ctx->gpr[rs] + ctx->gpr[rt];
            return 1;

        case 0x24:  // and
            ctx->gpr[rd] = ctx->gpr[rs] & ctx->gpr[rt];
            return 1;

        case 0x27:  // nor
            ctx->gpr[rd] = ~(ctx->gpr[rs] | ctx->gpr[rt]);
            return 1;

        case 0x25:  // or
            ctx->gpr[rd] = ctx->gpr[rs] | ctx->gpr[rt];
            return 1;

        case 0x04:  // sllv
            ctx->gpr[rd] = ctx->gpr[rt] << (ctx->gpr[rs] & 0x1F);
            return 1;

        case 0x07:  // srav
            ctx->gpr[rd] = (int32_t) ctx->gpr[rt] >> (ctx->gpr[rs] & 0x1F);
            return 1;

        case 0x06:  // srlv
            ctx->gpr[rd] = ctx->gpr[rt] >> (ctx->gpr[rs] & 0x1F);
            return 1;

        case 0x22:  // sub (w overflow)
            {
                uint32_t tmp = ctx->gpr[rs] - ctx->gpr[rt];
                if (((ctx->gpr[rs] ^ ctx->gpr[rt]) & (tmp ^ ctx->gpr[rs])) >> 31)
                    ctx->exception = MIPSVM_RC_INTEGER_OVERFLOW;
                else
                    ctx->gpr[rd] = tmp;
            }
            return 1;

        case 0x23:  // subu (w/o overflow)
            ctx->gpr[rd] = ctx->gpr[rs] - ctx->gpr[rt];
            return 1;

        case 0x26:  // xor
            ctx->gpr[rd] = ctx->gpr[rs] ^ ctx->gpr[rt];
            return 1;
        }
    }

    if (rs == 0)    // rs unused
    {
        switch (func)
        {
        case 0x00:  // sll
            ctx->gpr[rd] = ctx->gpr[rt] << aux;
            return 1;

        case 0x02:  // srl
            ctx->gpr[rd] = ctx->gpr[rt] >> aux;
            return 1;

        case 0x03:  // sra
            ctx->gpr[rd] = (int32_t) ctx->gpr[rt] >> aux;
            return 1;
        }
    }

    if (rt == 0)    // rt unused
    {
        switch (func)
        {
        case 0x09:  // jalr
            ctx->gpr[rd] = ctx->pc + 4;
            schedule_abs_branch(ctx, rs ? ctx->gpr[rs] : 0);    // XXX: r0 should be always 0
            return 1;
        }
    }

    switch (func)
    {

    case 0x0D:  // break
        ctx->code = (instr << 6) >> 12;
        ctx->exception = MIPSVM_RC_BREAK;
        return 1;

    case 0x02:
        if (rs == 1)   // rotr
        {
            ctx->gpr[rd] = (ctx->gpr[rt] >> aux) | (ctx->gpr[rt] << (32 - aux));
            return 1;
        }
        break;

    case 0x06:
        if (aux == 1)  // rotrv
        {
            int s = ctx->gpr[rs] & 0x1F;
            ctx->gpr[rd] = (ctx->gpr[rt] >> s) | (ctx->gpr[rt] << (32 - s));
            return 1;
        }
        break;

    case 0x0C:  // syscall
        ctx->code = (instr << 6) >> 12;
        ctx->exception = MIPSVM_RC_SYSCALL;
        return 1;

    case 0x34:  // teq
        if (ctx->gpr[rs] == ctx->gpr[rt])
        {
            ctx->code = (instr >> 6) & 0x3FF;
            ctx->exception = MIPSVM_RC_TRAP;
        }
        return 1;

    case 0x30:  // tge
        if ((int32_t)ctx->gpr[rs] >= (int32_t)ctx->gpr[rt])
        {
            ctx->code = (instr >> 6) & 0x3FF;
            ctx->exception = MIPSVM_RC_TRAP;
        }
        return 1;

    case 0x31:  // tgeu
        if (ctx->gpr[rs] >= ctx->gpr[rt])
        {
            ctx->code = (instr >> 6) & 0x3FF;
            ctx->exception = MIPSVM_RC_TRAP;
        }
        return 1;

    case 0x32:  // tlt
        if ((int32_t)ctx->gpr[rs] < (int32_t)ctx->gpr[rt])
        {
            ctx->code = (instr >> 6) & 0x3FF;
            ctx->exception = MIPSVM_RC_TRAP;
        }
        return 1;

    case 0x33:  // tltu
        if (ctx->gpr[rs] < ctx->gpr[rt])
        {
            ctx->code = (instr >> 6) & 0x3FF;
            ctx->exception = MIPSVM_RC_TRAP;
        }
        return 1;

    case 0x36:  // tltu
        if (ctx->gpr[rs] != ctx->gpr[rt])
        {
            ctx->code = (instr >> 6) & 0x3FF;
            ctx->exception = MIPSVM_RC_TRAP;
        }
        return 1;
    }

    return 0;
}

static bool exec_special2(mipsvm_t *ctx, uint32_t instr)
{
    const int func = instr & 0x3F;
    const int rs = (instr >> 21) & 0x1F;
    const int rt = (instr >> 16) & 0x1F;
    const int rd = (instr >> 11) & 0x1F;
    const int aux = (instr >> 6) & 0x1F;

    if (rd == 0 && aux == 0)
    {
        switch (instr)
        {
        case 0x00:  // madd
            ctx->acc += ((int64_t) (int32_t) ctx->gpr[rs]) * (int32_t) ctx->gpr[rt];
            return 1;

        case 0x01:  // maddu
            ctx->acc += (uint64_t) ctx->gpr[rs] * ctx->gpr[rt];
            return 1;

        case 0x04:  // msub
            ctx->acc -= ((int64_t) (int32_t) ctx->gpr[rs]) * (int32_t) ctx->gpr[rt];
            return 1;

        case 0x05:  // msubu
            ctx->acc -= (uint64_t) ctx->gpr[rs] * ctx->gpr[rt];
            return 1;
        }
    }

    if (aux == 0)
    {
        switch (func)
        {
        case 0x02:  // mul
            ctx->gpr[rd] = (int32_t)ctx->gpr[rs] * (int32_t)ctx->gpr[rt];
            return 1;

        // NOTE: no reason to call native hardware clz, we are VM and slow anyway
        case 0x20:  // clz
            if (! ctx->gpr[rs])
                ctx->gpr[rd] = 32;
            else
            {
                int i = 0;
                for (uint32_t tmp = ctx->gpr[rs]; ! (tmp & 0x80000000); tmp <<= 1, i++);
                ctx->gpr[rd] = i;
            }
            return 1;

        case 0x21:  // clo
            if (ctx->gpr[rs] == 0xFFFFFFFF)
                ctx->gpr[rd] = 32;
            else
            {
                int i = 0;
                for (uint32_t tmp = ctx->gpr[rs]; tmp & 0x80000000; tmp <<= 1, i++);
                ctx->gpr[rd] = i;
            }
            return 1;
        }
    }

    return 0;
}

static bool exec_special3(mipsvm_t *ctx, uint32_t instr)
{
    const int rs = (instr >> 21) & 0x1F;
    const int rt = (instr >> 16) & 0x1F;
    const int rd = (instr >> 11) & 0x1F;
    const int aux = (instr >> 6) & 0x1F;
    const int func = instr & 0x3F;

    if (rs == 0 && (func == 0x20))    // bshfl ? wtf name
    {
        switch (aux)
        {
        case 0x10:  // seb
            ctx->gpr[rd] = (int32_t)(int8_t) ctx->gpr[rt];
            return 1;

        case 0x18: // seh
            ctx->gpr[rd] = (int32_t)(int16_t) ctx->gpr[rt];
            return 1;

        case 0x02:  // wsbh
            {
                uint32_t tmp = ctx->gpr[rt];
                tmp = ((tmp & 0x00FF0000) << 8) | ((tmp & 0xFF000000) >> 8 ) | ((tmp & 0x000000FF) << 8) | ((tmp & 0x0000FF00) >> 8);
                ctx->gpr[rd] = tmp;
            }
            return 1;
        }
    }

    if (func == 0x00)   //ext
    {
        uint32_t src = ctx->gpr[rs];
        src <<= 32 - aux - rd - 1;      // remove msbits
        src >>= 32 - rd - 1;            // remove lsbits and right-align
        ctx->gpr[rt] = src;
        return 1;
    }

    if (func == 0x04)   // ins
    {
        int lsb = aux;
        int msb = rd;

        uint32_t src = ctx->gpr[rs];
        src <<= 32 - (msb - lsb + 1);   // clear src msbits
        src >>= 32 - (msb + 1);         // align

        uint32_t mask = 0xFFFFFFFF;
        mask <<= 32 - (msb - lsb + 1);   // clear mask msbits
        mask >>= 32 - (msb + 1);         // align

        ctx->gpr[rt] = (ctx->gpr[rt] & ~mask) | src;
        return 1;
    }

    return 0;
}

static bool exec_jtype(mipsvm_t *ctx, uint32_t instr)
{
    uint32_t targ = (instr << 8) >> 6;

    switch (instr >> 26)
    {
    case 0x02:  // j
        schedule_abs_branch(ctx, (ctx->pc & 0xF0000000) | targ);
        return 1;

    case 0x03:  // jal
        ctx->gpr[31] = ctx->pc + 4;
        schedule_abs_branch(ctx, (ctx->pc & 0xF0000000) | targ);
        return 1;
    }
    return 0;
}

static bool exec_itype(mipsvm_t *ctx, uint32_t instr)
{
    const int opcode = instr >> 26;
    const int rs = (instr >> 21) & 0x1F;
    const int rt = (instr >> 16) & 0x1F;
    const uint32_t imm_ze = (instr & 0xFFFF);
    const int32_t imm_se = (int16_t)(instr & 0xFFFF);

    if (opcode == 1)    // regimm
    {
        switch (rt)
        {
        case 0x0: // bltz
            if ((int32_t)ctx->gpr[rs] < 0)
                schedule_rel_branch(ctx, imm_se << 2);
            return 1;

        case 0x01:  // bgez
            if ((int32_t)ctx->gpr[rs] >= 0)
                schedule_rel_branch(ctx, imm_se << 2);
            return 1;

        case 0x10: // bltzal
            if ((int32_t)ctx->gpr[rs] < 0)
            {
                ctx->gpr[31] = ctx->pc + 4;
                schedule_rel_branch(ctx, imm_se << 2);
            }
            return 1;

        case 0x11: // bgezal
            if ((int32_t)ctx->gpr[rs] >= 0)
            {
                ctx->gpr[31] = ctx->pc + 4;
                schedule_rel_branch(ctx, imm_se << 2);
            }
            return 1;

        case 0x0C:  // teqi
            if (ctx->gpr[rs] == (uint32_t)imm_se)
                ctx->exception = MIPSVM_RC_TRAP;
            return 1;

        case 0x08:  // tgei
            if ((int32_t)ctx->gpr[rs] >= imm_se)
                ctx->exception = MIPSVM_RC_TRAP;
            return 1;

        case 0x09:  // tgeiu
            if (ctx->gpr[rs] >= (uint32_t)imm_se)
                ctx->exception = MIPSVM_RC_TRAP;
            return 1;

        case 0x0A:  // tlti
            if ((int32_t)ctx->gpr[rs] < imm_se)
                ctx->exception = MIPSVM_RC_TRAP;
            return 1;

        case 0x0B:  // tltiu
            if (ctx->gpr[rs] < (uint32_t)imm_se)
                ctx->exception = MIPSVM_RC_TRAP;
            return 1;

        case 0x0E:  // tnei
            if (ctx->gpr[rs] != (uint32_t)imm_se)
                ctx->exception = MIPSVM_RC_TRAP;
            return 1;
        }
    }

    if (rt == 0)
    {
        switch (opcode)
        {
        case 0x07:  // bgtz
            if ((int32_t)ctx->gpr[rs] > 0)
                schedule_rel_branch(ctx, imm_se << 2);
            return 1;

        case 0x06:  // blez
            if ((int32_t)ctx->gpr[rs] <= 0)
                schedule_rel_branch(ctx, imm_se << 2);
            return 1;
        }
    }

    switch (opcode)
    {
    case 0x08:  // addi (w ovf)
        {
            uint32_t tmp = ctx->gpr[rs] + imm_se;
            if (((tmp ^ ctx->gpr[rs]) & (tmp ^ imm_se)) >> 31)
                ctx->exception = MIPSVM_RC_INTEGER_OVERFLOW;
            else
                ctx->gpr[rt] = tmp;
        }
        return 1;

    case 0x09:  // addiu (wo ovf)
        ctx->gpr[rt] = ctx->gpr[rs] + imm_se;
        return 1;

    case 0x0C:  // andi
        ctx->gpr[rt] = ctx->gpr[rs] & imm_ze;
        return 1;

    case 0x04:  // beq
        if (ctx->gpr[rs] == ctx->gpr[rt])
            schedule_rel_branch(ctx, imm_se << 2);
        return 1;

    case 0x05:  // bne
        if (ctx->gpr[rs] != ctx->gpr[rt])
            schedule_rel_branch(ctx, imm_se << 2);
        return 1;

    case 0x20:  // lb
        ctx->gpr[rt] = (int32_t)(int8_t)readb(ctx, ctx->gpr[rs] + imm_se);
        return 1;

    case 0x24:  // lbu
        ctx->gpr[rt] = readb(ctx, ctx->gpr[rs] + imm_se);
        return 1;

    case 0x21:  // lh
        ctx->gpr[rt] = (int32_t)(int16_t)readh(ctx, ctx->gpr[rs] + imm_se);
        return 1;

    case 0x25:  // lhu
        ctx->gpr[rt] = readh(ctx, ctx->gpr[rs] + imm_se);
        return 1;

    case 0x0F:
        if (rs == 0)    // lui
        {
            ctx->gpr[rt] = imm_ze << 16;
            return 1;
        }
        break;

    case 0x23:  // lw
        ctx->gpr[rt] = readw(ctx, ctx->gpr[rs] + imm_se);
        return 1;

    case 0x22:  // lwl
        {
            // little-endian mode
            uint32_t addr = ctx->gpr[rs] + imm_se;
            uint32_t offset = addr & 0x03;
            uint32_t word = readw(ctx, addr & -4U);
            uint32_t reg = ctx->gpr[rt];

            reg &= 0x00FFFFFF >> (offset * 8);    // 0x00FFFFFF, 0x0000FFFF, 0x000000FF, 0x00000000
            word <<= 24 - (offset * 8);           // 24, 16, 8, 0
            ctx->gpr[rt] = reg | word;
        }
        return 1;

    case 0x26:  // lwr
        {
            // little-endian mode
            uint32_t addr = ctx->gpr[rs] + imm_se;
            uint32_t offset = addr & 0x03;
            uint32_t word = readw(ctx, addr & -4U);
            uint32_t reg = ctx->gpr[rt];

            reg &= ~(0xFFFFFFFF >> (offset * 8));    // ~0xFFFFFFFF, ~0x00FFFFFF, ~0x0000FFFF, ~0x000000FF
            word >>= offset * 8;           // 0, 8, 16, 24
            ctx->gpr[rt] = reg | word;
        }
        return 1;

    case 0x0D:  //ori
        ctx->gpr[rt] = ctx->gpr[rs] | imm_ze;
        return 1;

    case 0x28:  // sb
        writeb(ctx, ctx->gpr[rs] + imm_se, ctx->gpr[rt]);
        return 1;

    case 0x0A:  // slti
        ctx->gpr[rt] = (int32_t) ctx->gpr[rs] < imm_se;
        return 1;

    case 0x0B:  // sltiu
        ctx->gpr[rt] = ctx->gpr[rs] < (uint32_t)imm_se;
        return 1;

    case 0x29:  // sh
        writeh(ctx, ctx->gpr[rs] + imm_se, ctx->gpr[rt]);
        return 1;

    case 0x2B:  // sw
        writew(ctx, ctx->gpr[rs] + imm_se, ctx->gpr[rt]);
        return 1;

    case 0x2A:  // swl
        {
            // little-endian mode
            uint32_t addr = ctx->gpr[rs] + imm_se;
            uint32_t base = addr & -4U;
            uint32_t offset = addr & 0x03;
            uint32_t reg = ctx->gpr[rt];
            if (offset == 0)
            {
                writeb(ctx, base, reg >> 24);
            }
            else if (offset == 1)
            {
                writeh(ctx, base, reg >> 16);
            }
            else if (offset == 2)
            {
                writeh(ctx, base, reg >> 8);
                writeb(ctx, base + 2, reg >> 24);
            }
            else
            {
                writew(ctx, base, reg);
            }
        }
        return 1;

    case 0x2E:  // swr
        {
            // little-endian mode
            uint32_t addr = ctx->gpr[rs] + imm_se;
            uint32_t base = addr & -4U;
            uint32_t offset = addr & 0x03;
            uint32_t reg = ctx->gpr[rt];
            if (offset == 0)
            {
                writew(ctx, base, reg); // hgfe
            }
            else if (offset == 1)
            {
                writeb(ctx, base + 1, reg); // h
                writeh(ctx, base + 2, reg >> 8); // gf
            }
            else if (offset == 2)
            {
                writeh(ctx, base + 2, reg); // hg
            }
            else
            {
                writeb(ctx, base + 3, reg); // h
            }
        }
        return 1;

    case 0x0E:  // xori
        ctx->gpr[rt] = ctx->gpr[rs] ^ imm_ze;
        return 1;

    case 0x30:  // ll
        break;

    case 0x38:  // sc
        break;
    }

    return 0;
}

mipsvm_rc_t mipsvm_exec(mipsvm_t *ctx)
{
    // initial state before each instruction
    ctx->gpr[0] = 0;    // r0 always == 0
    ctx->exception = 0; // clean previous exception if any

    uint32_t instr = readw(ctx, ctx->pc);
    if (! ctx->branch_is_pending)
    {
        ctx->pc += 4;
    }
    else
    {
        ctx->branch_is_pending = 0;
        ctx->pc = ctx->branch_pc;
    }

    uint32_t opcode = instr >> 26;  // 6 top bits is the opcode

    bool was_decoded = 0;

    if (opcode == 0x00)
        was_decoded = exec_special(ctx, instr);
    else if (opcode == 0x1C)
        was_decoded = exec_special2(ctx, instr);
    else if (opcode == 0x1F)
        was_decoded = exec_special3(ctx, instr);
    else if ((opcode & 0x3E) == 0x02)
        was_decoded = exec_jtype(ctx, instr);
    else if ((opcode & 0x3C) != 0x10)
        was_decoded = exec_itype(ctx, instr);

    if (ctx->exception)
        return ctx->exception;

    return was_decoded ? MIPSVM_RC_OK : MIPSVM_RC_RESERVED_INSTR;
}

void mipsvm_init(mipsvm_t *ctx, const mipsvm_iface_t *iface, uint32_t reset_pc)
{
    memset(ctx, 0, sizeof(ctx));
    ctx->iface = *iface;
    ctx->pc = reset_pc;
}

uint32_t mipsvm_get_callcode(const mipsvm_t *ctx)
{
    return ctx->code;
}
