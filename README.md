mipsvm
=====
mipsvm is an embeddable virtual machine for executing MIPS32r2 instructions.

If you are interested in similar project for the AVR instruction set, see https://github.com/jkmnt/avrvm

Sometimes applications require user scripting. The common solution is to embed a scripting language (Python, Ruby, Lua, Javascript) running in a safe sandboxed environment and to provide a host API for interaction with application.
Small embedded projects may benefit from scripting too, but limited resources prevent us from using a full-scale scripting engine.

User script may be interpreted in runtime or compiled to bytecode for execution in virtual machine.
Most scripting languages are compiled to bytecode since running bytecode in VM is a way faster than interpreting.
When designing scripting language, one should decide on VM type (stack or register based), instruction set, language grammar and features. Tools such as a compiler should be created too. Too much trouble for a small scripting needs !

What if we could use an existing and proven VM instruction set, free tools and familiar language ?

Enter mipsvm :-) Instruction set is MIPS32r2, almost a classic RISC. Language is C, assembly or anything compilable to the MIPS. In case of C, Codesourcery CodeBench Lite MIPS (elf) toolchain may be used for script compiling.

Features
--------
* mipsvm have no external dependencies
* basic mipsvm API is just two functions - mipsvm_init and mipsvm_exec plus a few callbacks for interfacing with memory
* bytecode may be fetched from any media with random access (internal flash, ram, sdcard, you name it)
* easy multitasking - script may be paused on calling async host function and resumed upon receiving response some time later. For the script, the call will look like a plain blocking call.
* several instances of VMs may be running in parallel, cooperatively sharing time if control is passed between VMs in a round-robin way.

Limitations
-----------
* no coprocessors emulation, even CP0. Just a basic RISC machine.
* no floating point support. In MIPS32, there is usually a FP coprocessor CP1. In mipsvm, there is not. Use soft-float or fork a project if floats are required.
* no emulation of instructions timing.
* no interrupts.
* no hardware, memory regions, MMU, etc. emulation. Just a CPU.

Host interface
--------------
Host API functions may be called from script via MIPS 'syscall' instruction.
Another way to interact with script is to use a shared memory.

VM API
---
VM instance is initialized via call to mipsvm_init.

    static mipsvm_ctx_t vm;
    static const mipsvm_iface_t iface =
    {
        .readb = byte_reader,
        .readh = hword_reader,
        .readw = word_reader,
        .writeb = byte_writer,
        .writeh = hword_writer,
        .writew = word_writer,
    };
    mipsvm_init(&vm, &iface, RESET_PC);

Script is executed by repeatedly calling mipsvm_exec. Single instruction is executed.

    mipsvm_rc_t res = mipsvm_exec(&vm);

Return value maps to

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

In case of MIPSVM_RC_SYSCALL, use mipsvm_get_callcode(&vm) function to obtain syscall index.

Memory interface functions (word_reader/word_writer/etc.) may implement MMU emulation if required.
