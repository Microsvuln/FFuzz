/// Copyright (C) 2003  Fabrice Bellard
/// Copyright (C) 2010  Dependable Systems Laboratory, EPFL
/// Copyright (C) 2016  Cyberhaven, Inc
/// Copyrights of all contributions belong to their respective owners.
///
/// This library is free software; you can redistribute it and/or
/// modify it under the terms of the GNU Library General Public
/// License as published by the Free Software Foundation; either
/// version 2 of the License, or (at your option) any later version.
///
/// This library is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
/// Library General Public License for more details.
///
/// You should have received a copy of the GNU Library General Public
/// License along with this library; if not, see <http://www.gnu.org/licenses/>.

#include "qemu-timer.h"
#include <cpu/memory.h>
#include "exec-phys.h"

#define DATA_SIZE (1 << SHIFT)

#if DATA_SIZE == 8
#define SUFFIX q
#define USUFFIX q
#define DATA_TYPE uint64_t
#elif DATA_SIZE == 4
#define SUFFIX l
#define USUFFIX l
#define DATA_TYPE uint32_t
#elif DATA_SIZE == 2
#define SUFFIX w
#define USUFFIX uw
#define DATA_TYPE uint16_t
#elif DATA_SIZE == 1
#define SUFFIX b
#define USUFFIX ub
#define DATA_TYPE uint8_t
#else
#error unsupported data size
#endif

#ifdef SOFTMMU_CODE_ACCESS
#define READ_ACCESS_TYPE 2
#define ADDR_READ addr_code
#else
#define READ_ACCESS_TYPE 0
#define ADDR_READ addr_read
#endif

#ifndef CONFIG_TCG_PASS_AREG0
#define ENV_PARAM
#define ENV_VAR
#define CPU_PREFIX
#define HELPER_PREFIX __
#else
#define ENV_PARAM CPUArchState *env,
#define ENV_VAR env,
#define CPU_PREFIX cpu_
#define HELPER_PREFIX helper_
#endif

#define ADDR_MAX ((target_ulong)-1)

#ifdef CONFIG_SYMBEX
#include <cpu/se_qemu_config.h>

#if defined(SYMBEX_LLVM_LIB) && !defined(STATIC_TRANSLATOR)
    #define INSTR_BEFORE_MEMORY_ACCESS(vaddr, value, flags) \
        if (*g_sqi.events.before_memory_access_signals_count) tcg_llvm_before_memory_access(vaddr, value, sizeof(value), flags);
    #define INSTR_AFTER_MEMORY_ACCESS(vaddr, value, flags, retaddr) \
        if (*g_sqi.events.after_memory_access_signals_count) tcg_llvm_after_memory_access(vaddr, value, sizeof(value), flags, 0);
    #define INSTR_FORK_AND_CONCRETIZE(val, max) \
        tcg_llvm_fork_and_concretize(val, 0, max, 0)
    #define SE_SET_MEM_IO_VADDR(env, addr, reset) \
        tcg_llvm_write_mem_io_vaddr(addr, reset)
#else // SYMBEX_LLVM_LIB
    #if defined(SE_ENABLE_MEM_TRACING) && !defined(STATIC_TRANSLATOR)
        #ifdef SOFTMMU_CODE_ACCESS
            #define INSTR_BEFORE_MEMORY_ACCESS(vaddr, value, flags)
            #define INSTR_AFTER_MEMORY_ACCESS(vaddr, value, flags, retaddr)
        #else
            #define INSTR_BEFORE_MEMORY_ACCESS(vaddr, value, flags)
            #define INSTR_AFTER_MEMORY_ACCESS(vaddr, value, flags, retaddr) \
                if (unlikely(*g_sqi.events.after_memory_access_signals_count)) g_sqi.events.after_memory_access(vaddr, value, sizeof(value), flags, (uintptr_t) 0);
        #endif
    #else
        #define INSTR_BEFORE_MEMORY_ACCESS(vaddr, value, flags)
        #define INSTR_AFTER_MEMORY_ACCESS(vaddr, value, flags, retaddr)
    #endif

    #define INSTR_FORK_AND_CONCRETIZE(val, max) (val)

    #define SE_SET_MEM_IO_VADDR(env, addr, reset) \
            env->mem_io_vaddr = addr;

#endif // SYMBEX_LLVM_LIB

#define INSTR_FORK_AND_CONCRETIZE_ADDR(val, max) \
    (*g_sqi.mode.fork_on_symbolic_address ? INSTR_FORK_AND_CONCRETIZE(val, max) : val)

#define SE_RAM_OBJECT_DIFF (TARGET_PAGE_BITS - SE_RAM_OBJECT_BITS)

#else // CONFIG_SYMBEX

#define INSTR_BEFORE_MEMORY_ACCESS(...)
#define INSTR_AFTER_MEMORY_ACCESS(...)
#define INSTR_FORK_AND_CONCRETIZE(val, max) (val)
#define INSTR_FORK_AND_CONCRETIZE_ADDR(val, max) (val)

#define SE_RAM_OBJECT_BITS TARGET_PAGE_BITS
#define SE_RAM_OBJECT_SIZE TARGET_PAGE_SIZE
#define SE_RAM_OBJECT_MASK TARGET_PAGE_MASK
#define SE_RAM_OBJECT_DIFF 0

#define SE_SET_MEM_IO_VADDR(env, addr, reset) \
        env->mem_io_vaddr = addr;

#endif // CONFIG_SYMBEX

static DATA_TYPE glue(glue(slow_ld, SUFFIX), MMUSUFFIX)(target_ulong addr,
                                                        int mmu_idx,
                                                        void *retaddr);

DATA_TYPE glue(glue(io_read, SUFFIX), MMUSUFFIX)(ENV_PARAM
                                              target_phys_addr_t physaddr,
                                              target_ulong addr,
                                              void *retaddr);

#if defined(STATIC_TRANSLATOR)
inline DATA_TYPE glue(glue(io_read_chk, SUFFIX), MMUSUFFIX)(ENV_PARAM target_phys_addr_t physaddr,
                                          target_ulong addr,
                                          void *retaddr)
{
    assert(false && "Cannot run statically");
}


#elif !defined(SYMBEX_LLVM_LIB)
DATA_TYPE glue(glue(io_read, SUFFIX), MMUSUFFIX)(ENV_PARAM
                                              target_phys_addr_t physaddr,
                                              target_ulong addr,
                                              void *retaddr)
{
    DATA_TYPE res;
    const struct MemoryDescOps *ops = phys_get_ops(physaddr);

    physaddr = (physaddr & TARGET_PAGE_MASK) + addr;

#if defined(CONFIG_SYMBEX) && defined(CONFIG_SYMBEX_MP)
    assert(false && "Not implemented");
#endif

    env->mem_io_pc = (uintptr_t)retaddr;

    SE_SET_MEM_IO_VADDR(env, addr, 0);
#if SHIFT <= 2
    res = ops->read(physaddr, 1 << SHIFT);
#else
#ifdef TARGET_WORDS_BIGENDIAN
    res = ops->read(physaddr, 4) << 32;
    res |= ops->read(physaddr + 4, 4);
#else
    res = ops->read(physaddr, 4);
    res |= ops->read(physaddr + 4, 4) << 32;
#endif
#endif /* SHIFT > 2 */
    return res;
}

inline DATA_TYPE glue(glue(io_read_chk, SUFFIX), MMUSUFFIX)(ENV_PARAM target_phys_addr_t physaddr,
                                          target_ulong addr,
                                          void *retaddr)
{
    return glue(glue(io_read, SUFFIX), MMUSUFFIX)(ENV_VAR physaddr, addr, retaddr);
}


#elif defined(SYMBEX_LLVM_LIB) //SYMBEX_LLVM_LIB

inline DATA_TYPE glue(glue(io_read_chk, SUFFIX), MMUSUFFIX)(ENV_PARAM target_phys_addr_t physaddr,
                                          target_ulong addr,
                                          void *retaddr)
{
    assert(false && "Not implemented");
    return 0;
}


#endif

#ifndef STATIC_TRANSLATOR
/* handle all cases except unaligned access which span two pages */
DATA_TYPE
glue(glue(glue(HELPER_PREFIX, ld), SUFFIX), MMUSUFFIX)(ENV_PARAM
                                                       target_ulong addr,
                                                       int mmu_idx)
{
    DATA_TYPE res;
    target_ulong object_index, index;
    target_ulong tlb_addr;
    target_phys_addr_t ioaddr;
    void *retaddr = NULL;
    CPUTLBEntry *tlb_entry;

    /* test if there is match for unaligned or IO access */
    /* XXX: could done more in memory macro in a non portable way */
#ifdef CONFIG_SYMBEX_MP
    INSTR_BEFORE_MEMORY_ACCESS(addr, 0, 0);
    addr = INSTR_FORK_AND_CONCRETIZE_ADDR(addr, ADDR_MAX);
    object_index = INSTR_FORK_AND_CONCRETIZE(addr >> SE_RAM_OBJECT_BITS,
                                           ADDR_MAX >> SE_RAM_OBJECT_BITS);
    index = (object_index >> SE_RAM_OBJECT_DIFF) & (CPU_TLB_SIZE - 1);
#else
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    object_index = index;
#endif

 redo:
    tlb_entry = &env->tlb_table[mmu_idx][index];
    tlb_addr = tlb_entry->ADDR_READ;
    if (likely((addr & TARGET_PAGE_MASK) == (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK)))) {
        if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
            /* IO access */
            if ((addr & (DATA_SIZE - 1)) != 0)
                goto do_unaligned_access;
#ifndef SYMBEX_LLVM_LIB
            retaddr = GETPC();
#endif
            ioaddr = env->iotlb[mmu_idx][index];
#ifdef CONFIG_SYMBEX
            env->se_tlb_current = tlb_entry;
#endif
            res = glue(glue(io_read_chk, SUFFIX), MMUSUFFIX)(ENV_VAR ioaddr, addr, retaddr);

            INSTR_AFTER_MEMORY_ACCESS(addr, res, MEM_TRACE_FLAG_IO, retaddr);

        } else if (unlikely(((addr & ~SE_RAM_OBJECT_MASK) + DATA_SIZE - 1) >= SE_RAM_OBJECT_SIZE)) {
            /* slow unaligned access (it spans two pages or IO) */
        do_unaligned_access:
#ifndef SYMBEX_LLVM_LIB
            retaddr = GETPC();
#endif
#ifdef ALIGNED_ONLY
            do_unaligned_access(ENV_VAR addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
#endif
            res = glue(glue(slow_ld, SUFFIX), MMUSUFFIX)(ENV_VAR addr,
                                                         mmu_idx, retaddr);
        } else {
            /* unaligned/aligned access in the same page */
#ifdef ALIGNED_ONLY
            if ((addr & (DATA_SIZE - 1)) != 0) {
#ifndef SYMBEX_LLVM_LIB
                retaddr = GETPC();
#endif
                do_unaligned_access(ENV_VAR addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
            }
#endif

#if defined(CONFIG_SYMBEX) && !defined(SYMBEX_LLVM_LIB) && defined(CONFIG_SYMBEX_MP)
            res = glue(glue(ld, USUFFIX), _p)((uint8_t *)(intptr_t)(addr+tlb_entry->se_addend));
#else
            res = glue(glue(ld, USUFFIX), _p)((uint8_t *)(intptr_t)(addr+tlb_entry->addend));
#endif
            INSTR_AFTER_MEMORY_ACCESS(addr, res, 0, retaddr);
        }
    } else {
        /* the page is not in the TLB : fill it */
#ifndef SYMBEX_LLVM_LIB
        retaddr = GETPC();
#endif
#ifdef ALIGNED_ONLY
        if ((addr & (DATA_SIZE - 1)) != 0)
            do_unaligned_access(ENV_VAR addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
#endif
        tlb_fill(env, addr, object_index << SE_RAM_OBJECT_BITS,
                 READ_ACCESS_TYPE, mmu_idx, retaddr);
        goto redo;
    }

    return res;
}
#endif /* STATIC_TRANSLATOR */

/* handle all unaligned cases */
static DATA_TYPE
glue(glue(slow_ld, SUFFIX), MMUSUFFIX)(ENV_PARAM
                                       target_ulong addr,
                                       int mmu_idx,
                                       void *retaddr)
{
    DATA_TYPE res, res1, res2;
    target_ulong object_index, index, shift;
    target_phys_addr_t ioaddr;
    target_ulong tlb_addr, addr1, addr2;
    CPUTLBEntry *tlb_entry;

    INSTR_BEFORE_MEMORY_ACCESS(addr, 0, 0);
    addr = INSTR_FORK_AND_CONCRETIZE_ADDR(addr, ADDR_MAX);
    object_index = INSTR_FORK_AND_CONCRETIZE(addr >> SE_RAM_OBJECT_BITS,
                                           ADDR_MAX >> SE_RAM_OBJECT_BITS);
    index = (object_index >> SE_RAM_OBJECT_DIFF) & (CPU_TLB_SIZE - 1);
 redo:
    tlb_entry = &env->tlb_table[mmu_idx][index];
    tlb_addr = tlb_entry->ADDR_READ;
    if ((addr & TARGET_PAGE_MASK) == (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (tlb_addr & ~TARGET_PAGE_MASK) {
            /* IO access */
            if ((addr & (DATA_SIZE - 1)) != 0)
                goto do_unaligned_access;
            ioaddr = env->iotlb[mmu_idx][index];

#ifdef CONFIG_SYMBEX
            env->se_tlb_current = tlb_entry;
#endif
            res = glue(glue(io_read_chk, SUFFIX), MMUSUFFIX)(ENV_VAR ioaddr, addr, retaddr);

            INSTR_AFTER_MEMORY_ACCESS(addr, res, MEM_TRACE_FLAG_IO, retaddr);
        } else if (((addr & ~SE_RAM_OBJECT_MASK) + DATA_SIZE - 1) >= SE_RAM_OBJECT_SIZE) {

        do_unaligned_access:
            /* slow unaligned access (it spans two pages) */
            addr1 = addr & ~(DATA_SIZE - 1);
            addr2 = addr1 + DATA_SIZE;
            res1 = glue(glue(slow_ld, SUFFIX), MMUSUFFIX)(ENV_VAR addr1,
                                                          mmu_idx, retaddr);
            res2 = glue(glue(slow_ld, SUFFIX), MMUSUFFIX)(ENV_VAR addr2,
                                                          mmu_idx, retaddr);
            shift = (addr & (DATA_SIZE - 1)) * 8;
#ifdef TARGET_WORDS_BIGENDIAN
            res = (res1 << shift) | (res2 >> ((DATA_SIZE * 8) - shift));
#else
            res = (res1 >> shift) | (res2 << ((DATA_SIZE * 8) - shift));
#endif
            res = (DATA_TYPE)res;
        } else {
            /* unaligned/aligned access in the same page */
#if defined(CONFIG_SYMBEX) && !defined(SYMBEX_LLVM_LIB) && defined(CONFIG_SYMBEX_MP)
            res = glue(glue(ld, USUFFIX), _p)((uint8_t *)(intptr_t)(addr+tlb_entry->se_addend));
#else
            res = glue(glue(ld, USUFFIX), _p)((uint8_t *)(intptr_t)(addr+tlb_entry->addend));
#endif
            INSTR_AFTER_MEMORY_ACCESS(addr, res, 0, retaddr);
        }
    } else {
        /* the page is not in the TLB : fill it */
        tlb_fill(env, addr, object_index << SE_RAM_OBJECT_BITS,
                 READ_ACCESS_TYPE, mmu_idx, retaddr);
        goto redo;
    }
    return res;
}

/*************************************************************************************/

#ifndef SOFTMMU_CODE_ACCESS

static void glue(glue(slow_st, SUFFIX), MMUSUFFIX)(ENV_PARAM
                                                   target_ulong addr,
                                                   DATA_TYPE val,
                                                   int mmu_idx,
                                                   void *retaddr);

void glue(glue(io_write, SUFFIX), MMUSUFFIX)(
                                          ENV_PARAM target_phys_addr_t physaddr,
                                          DATA_TYPE val,
                                          target_ulong addr,
                                          void *retaddr);
#if defined(STATIC_TRANSLATOR)
inline void glue(glue(io_write_chk, SUFFIX), MMUSUFFIX)(ENV_PARAM target_phys_addr_t physaddr,
                                          DATA_TYPE val,
                                          target_ulong addr,
                                          void *retaddr)
{
    assert(false && "Cannot run statically");
}

#elif !defined(SYMBEX_LLVM_LIB)


void glue(glue(io_write, SUFFIX), MMUSUFFIX)(ENV_PARAM
                                             target_phys_addr_t physaddr,
                                          DATA_TYPE val,
                                          target_ulong addr,
                                          void *retaddr)
{
    const struct MemoryDescOps *ops = phys_get_ops(physaddr);

    physaddr = (physaddr & TARGET_PAGE_MASK) + addr;

#if defined(CONFIG_SYMBEX) && defined(CONFIG_SYMBEX_MP)
    assert(false && "Not implemented");
#endif

    SE_SET_MEM_IO_VADDR(env, addr, 0);
    env->mem_io_pc = (uintptr_t)retaddr;
#if SHIFT <= 2
    ops->write(physaddr, val, 1 << SHIFT);
#else
#ifdef TARGET_WORDS_BIGENDIAN
    ops->write(physaddr, (val >> 32), 4);
    ops->write(physaddr + 4, (uint32_t)val, 4);
#else
    ops->write(physaddr, (uint32_t)val, 4);
    ops->write(physaddr + 4, val >> 32, 4);
#endif
#endif /* SHIFT > 2 */
}

inline void glue(glue(io_write_chk, SUFFIX), MMUSUFFIX)(ENV_PARAM target_phys_addr_t physaddr,
                                          DATA_TYPE val,
                                          target_ulong addr,
                                          void *retaddr)
{
    //XXX: check symbolic memory mapped devices and write log here.
    glue(glue(io_write, SUFFIX), MMUSUFFIX)(ENV_VAR physaddr, val, addr, retaddr);
}

#else


/**
  * Only if compiling for LLVM.
  * This function checks whether a write goes to a clean memory page.
  * If yes, does the write directly.
  * This avoids symbolic values flowing outside the LLVM code and killing the states.
  *
  * It also deals with writes to memory-mapped devices that are symbolic
  */
inline void glue(glue(io_write_chk, SUFFIX), MMUSUFFIX)(ENV_PARAM target_phys_addr_t physaddr,
                                          DATA_TYPE val,
                                          target_ulong addr,
                                          void *retaddr)
{
    assert(false && "Not implemented");
}

#endif

#ifndef STATIC_TRANSLATOR
void glue(glue(glue(HELPER_PREFIX, st), SUFFIX), MMUSUFFIX)(ENV_PARAM
                                                            target_ulong addr,
                                                            DATA_TYPE val,
                                                            int mmu_idx)
{
    target_phys_addr_t ioaddr;
    target_ulong tlb_addr;
    void *retaddr = NULL;
    target_ulong object_index, index;
    CPUTLBEntry *tlb_entry;

#ifdef CONFIG_SYMBEX_MP
    INSTR_BEFORE_MEMORY_ACCESS(addr, val, 1);
    addr = INSTR_FORK_AND_CONCRETIZE_ADDR(addr, ADDR_MAX);
    object_index = INSTR_FORK_AND_CONCRETIZE(addr >> SE_RAM_OBJECT_BITS,
                                           ADDR_MAX >> SE_RAM_OBJECT_BITS);
    index = (object_index >> SE_RAM_OBJECT_DIFF) & (CPU_TLB_SIZE - 1);
#else
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    object_index = index;
#endif

 redo:
    tlb_entry = &env->tlb_table[mmu_idx][index];
    tlb_addr = tlb_entry->addr_write;
    if (likely((addr & TARGET_PAGE_MASK) == (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK)))) {
        if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
            /* IO access */
            if ((addr & (DATA_SIZE - 1)) != 0)
                goto do_unaligned_access;
#ifndef SYMBEX_LLVM_LIB
            retaddr = GETPC();
#endif
            ioaddr = env->iotlb[mmu_idx][index];

#ifdef CONFIG_SYMBEX
            env->se_tlb_current = tlb_entry;
#endif

            glue(glue(io_write_chk, SUFFIX), MMUSUFFIX)(ENV_VAR ioaddr, val, addr, retaddr);

            INSTR_AFTER_MEMORY_ACCESS(addr, val, MEM_TRACE_FLAG_IO | MEM_TRACE_FLAG_WRITE, retaddr);
        } else if (unlikely(((addr & ~SE_RAM_OBJECT_MASK) + DATA_SIZE - 1) >= SE_RAM_OBJECT_SIZE)) {

        do_unaligned_access:
#ifndef SYMBEX_LLVM_LIB
            retaddr = GETPC();
#endif
#ifdef ALIGNED_ONLY
            do_unaligned_access(ENV_VAR addr, 1, mmu_idx, retaddr);
#endif
            glue(glue(slow_st, SUFFIX), MMUSUFFIX)(ENV_VAR addr, val,
                                                   mmu_idx, retaddr);
        } else {
            /* aligned/unaligned access in the same page */
#ifdef ALIGNED_ONLY
            if ((addr & (DATA_SIZE - 1)) != 0) {
#ifndef SYMBEX_LLVM_LIB
                retaddr = GETPC();
#endif
                do_unaligned_access(ENV_VAR addr, 1, mmu_idx, retaddr);
            }
#endif

#if defined(CONFIG_SYMBEX) && !defined(SYMBEX_LLVM_LIB) && defined(CONFIG_SYMBEX_MP)
            glue(glue(st, SUFFIX), _p)((uint8_t*)(addr + tlb_entry->se_addend), val);
#else
            glue(glue(st, SUFFIX), _p)((uint8_t*)(addr + tlb_entry->addend), val);
#endif
            INSTR_AFTER_MEMORY_ACCESS(addr, val, MEM_TRACE_FLAG_WRITE, retaddr);
        }
    } else {
        /* the page is not in the TLB : fill it */
#ifndef SYMBEX_LLVM_LIB
        retaddr = GETPC();
#endif
#ifdef ALIGNED_ONLY
        if ((addr & (DATA_SIZE - 1)) != 0)
            do_unaligned_access(ENV_VAR addr, 1, mmu_idx, retaddr);
#endif
        tlb_fill(env, addr, object_index << SE_RAM_OBJECT_BITS,
                 1, mmu_idx, retaddr);
        goto redo;
    }
}
#endif /* STATIC_TRANSLATOR */

/* handles all unaligned cases */
static void glue(glue(slow_st, SUFFIX), MMUSUFFIX)(ENV_PARAM
                                                   target_ulong addr,
                                                   DATA_TYPE val,
                                                   int mmu_idx,
                                                   void *retaddr)
{
    target_phys_addr_t ioaddr;
    target_ulong tlb_addr;
    target_ulong object_index, index;
    int i;
    CPUTLBEntry *tlb_entry;

    INSTR_BEFORE_MEMORY_ACCESS(addr, val, 1);
    addr = INSTR_FORK_AND_CONCRETIZE_ADDR(addr, ADDR_MAX);
    object_index = INSTR_FORK_AND_CONCRETIZE(addr >> SE_RAM_OBJECT_BITS,
                                           ADDR_MAX >> SE_RAM_OBJECT_BITS);
    index = (object_index >> SE_RAM_OBJECT_DIFF) & (CPU_TLB_SIZE - 1);
 redo:
    tlb_entry = &env->tlb_table[mmu_idx][index];
    tlb_addr = tlb_entry->addr_write;
    if ((addr & TARGET_PAGE_MASK) == (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        if (tlb_addr & ~TARGET_PAGE_MASK) {
            /* IO access */
            if ((addr & (DATA_SIZE - 1)) != 0)
                goto do_unaligned_access;
            ioaddr = env->iotlb[mmu_idx][index];
#ifdef CONFIG_SYMBEX
            env->se_tlb_current = tlb_entry;
#endif
            glue(glue(io_write_chk, SUFFIX), MMUSUFFIX)(ENV_VAR ioaddr, val, addr, retaddr);

            INSTR_AFTER_MEMORY_ACCESS(addr, val, MEM_TRACE_FLAG_IO | MEM_TRACE_FLAG_WRITE, retaddr);
        } else if (((addr & ~SE_RAM_OBJECT_MASK) + DATA_SIZE - 1) >= SE_RAM_OBJECT_SIZE) {

        do_unaligned_access:
            /* XXX: not efficient, but simple */
            /* Note: relies on the fact that tlb_fill() does not remove the
             * previous page from the TLB cache.  */
            for(i = DATA_SIZE - 1; i >= 0; i--) {
#ifdef TARGET_WORDS_BIGENDIAN
                glue(slow_stb, MMUSUFFIX)(ENV_VAR addr + i,
                                          val >> (((DATA_SIZE - 1) * 8) - (i * 8)),
                                          mmu_idx, retaddr);
#else
                glue(slow_stb, MMUSUFFIX)(ENV_VAR addr + i,
                                          val >> (i * 8),
                                          mmu_idx, retaddr);
#endif
            }
        } else {
            /* aligned/unaligned access in the same page */

#if defined(CONFIG_SYMBEX) && !defined(SYMBEX_LLVM_LIB) && defined(CONFIG_SYMBEX_MP)
            glue(glue(st, SUFFIX), _p)((uint8_t*)(addr + tlb_entry->se_addend), val);
#else
            glue(glue(st, SUFFIX), _p)((uint8_t*)(addr + tlb_entry->addend), val);
#endif
            INSTR_AFTER_MEMORY_ACCESS(addr, val, MEM_TRACE_FLAG_WRITE, retaddr);
        }
    } else {
        /* the page is not in the TLB : fill it */
        tlb_fill(env, addr, object_index << SE_RAM_OBJECT_BITS,
                 1, mmu_idx, retaddr);
        goto redo;
    }
}

#endif /* !defined(SOFTMMU_CODE_ACCESS) */

#ifndef CONFIG_SYMBEX
#undef SE_RAM_OBJECT_BITS
#undef SE_RAM_OBJECT_SIZE
#undef SE_RAM_OBJECT_MASK
#endif
#undef INSTR_FORK_AND_CONCRETIZE_ADDR
#undef INSTR_FORK_AND_CONCRETIZE
#undef INSTR_AFTER_MEMORY_ACCESS
#undef INSTR_BEFORE_MEMORY_ACCESS
#undef ADDR_MAX
#undef READ_ACCESS_TYPE
#undef SHIFT
#undef DATA_TYPE
#undef SUFFIX
#undef USUFFIX
#undef DATA_SIZE
#undef ADDR_READ
#undef ENV_PARAM
#undef ENV_VAR
#undef CPU_PREFIX
#undef HELPER_PREFIX
