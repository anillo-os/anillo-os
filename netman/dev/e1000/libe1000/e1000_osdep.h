/******************************************************************************

  Copyright (c) 2001-2019, Intel Corporation
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

// Modified for usage in Anillo's netman under the same license

#ifndef _E1000_OSDEP_H_
#define _E1000_OSDEP_H_

#include <stdint.h>
#include <stdbool.h>
#include <libsys/libsys.h>
#include <libsimple/libsimple.h>
#include <netman/base.h>

#define e1000_osdep_delay(x, scale) ({ \
		__typeof__(x) _x = (x); \
		if (_x > 0) { \
			sys_abort_status(sys_thread_suspend_timeout(sys_thread_current(), (uint64_t)(_x) * (scale), sys_timeout_type_relative_ns_monotonic)); \
		} \
	})

#define usec_delay(x) e1000_osdep_delay(x, 1000)
#define msec_delay(x) e1000_osdep_delay(x, 1000000)

// we don't need IRQ-safe delays since we run in userspace
#define usec_delay_irq(x) usec_delay(x)
#define msec_delay_irq(x) msec_delay(x)

/* Enable/disable debugging statements in shared code */
#define DBG 0

#define DEBUGOUT(...) do { if (DBG) sys_console_log_f(__VA_ARGS__); } while (0)
#define DEBUGOUT1(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT2(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT3(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT7(...) DEBUGOUT(__VA_ARGS__)
#define DEBUGFUNC(F) DEBUGOUT(F "\n")

#define STATIC static
#define FALSE false
#define TRUE true

#define CMD_MEM_WRT_INVALIDATE /* TODO */ 0

/* Mutex used in the shared code */
#define E1000_MUTEX sys_mutex_t
#define E1000_MUTEX_INIT(mutex) sys_mutex_init(mutex)
#define E1000_MUTEX_DESTROY(mutex) /* no-op */
#define E1000_MUTEX_LOCK(mutex) sys_mutex_lock(mutex)
#define E1000_MUTEX_TRYLOCK(mutex) sys_mutex_try_lock(mutex)
#define E1000_MUTEX_UNLOCK(mutex) sys_mutex_unlock(mutex)

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

#define __le16 u16
#define __le32 u32
#define __le64 u64

#define E1000_REGISTER(hw, reg) (((hw)->mac.type >= e1000_82543) ? reg : e1000_translate_register_82542(reg))

#define E1000_WRITE_FLUSH(a) E1000_READ_REG(a, E1000_STATUS)

NETMAN_STRUCT_FWD(netman_e1000);

uint32_t netman_e1000_read_bar0(netman_e1000_t* nic, size_t offset);
void netman_e1000_write_bar0(netman_e1000_t* nic, size_t offset, uint32_t value);

uint32_t netman_e1000_flash_read_32(netman_e1000_t* nic, size_t offset);
void netman_e1000_flash_write_32(netman_e1000_t* nic, size_t offset, uint32_t value);

uint16_t netman_e1000_flash_read_16(netman_e1000_t* nic, size_t offset);
void netman_e1000_flash_write_16(netman_e1000_t* nic, size_t offset, uint16_t value);

/* Read from an absolute offset in the adapter's memory space */
#define E1000_READ_OFFSET(hw, offset) ({ \
		__typeof__(hw) _read_offset_hw = (hw); \
		__typeof__(offset) _read_offset_offset = (offset); \
		fassert((_read_offset_offset & 3) == 0); \
		netman_e1000_read_bar0((netman_e1000_t*)_read_offset_hw->back, _read_offset_offset); \
	})

/* Write to an absolute offset in the adapter's memory space */
#define E1000_WRITE_OFFSET(hw, offset, value) ({ \
		__typeof__(hw) _write_offset_hw = (hw); \
		__typeof__(offset) _write_offset_offset = (offset); \
		fassert((_write_offset_offset & 3) == 0); \
		netman_e1000_write_bar0((netman_e1000_t*)_write_offset_hw->back, _write_offset_offset, (value)); \
	})

/* Register READ/WRITE macros */

#define E1000_READ_REG(hw, reg) ({ \
		__typeof__(hw) _read_reg_hw = (hw); \
		__typeof__(reg) _read_reg_reg = (reg); \
		E1000_READ_OFFSET(_read_reg_hw, E1000_REGISTER(_read_reg_hw, _read_reg_reg)); \
	})

#define E1000_WRITE_REG(hw, reg, value) ({ \
		__typeof__(hw) _write_reg_hw = (hw); \
		__typeof__(reg) _write_reg_reg = (reg); \
		E1000_WRITE_OFFSET(_write_reg_hw, E1000_REGISTER(_write_reg_hw, _write_reg_reg), (value)); \
	})

#define E1000_READ_REG_ARRAY(hw, reg, index) ({ \
		__typeof__(hw) _read_reg_array_hw = (hw); \
		__typeof__(reg) _read_reg_array_reg = (reg); \
		E1000_READ_OFFSET(_read_reg_array_hw, E1000_REGISTER(_read_reg_array_hw, _read_reg_array_reg) + ((index) << 2)); \
	})

#define E1000_WRITE_REG_ARRAY(hw, reg, index, value) ({ \
		__typeof__(hw) _write_reg_array_hw = (hw); \
		__typeof__(reg) _write_reg_array_reg = (reg); \
		E1000_WRITE_OFFSET(_write_reg_array_hw, E1000_REGISTER(_write_reg_array_hw, _write_reg_array_reg) + ((index) << 2), (value)); \
	})

#define E1000_READ_REG_ARRAY_DWORD E1000_READ_REG_ARRAY
#define E1000_WRITE_REG_ARRAY_DWORD E1000_WRITE_REG_ARRAY

#define E1000_WRITE_REG_IO(hw, reg, value) /* TODO */

// 32-bit flash access

#define E1000_READ_FLASH_REG(hw, reg) ({ \
		__typeof__(hw) _hw = (hw); \
		__typeof__(reg) _reg = (reg); \
		netman_e1000_flash_read_32((netman_e1000_t*)_hw->back, _reg); \
	})

#define E1000_WRITE_FLASH_REG(hw, reg, value) ({ \
		__typeof__(hw) _hw = (hw); \
		__typeof__(reg) _reg = (reg); \
		netman_e1000_flash_write_32((netman_e1000_t*)_hw->back, _reg, (value)); \
	})

// 16-bit flash access
//
// not sure if the flash allows 16-bit addressing, so we'll use 32-bit addressing just to be safe

#define E1000_READ_FLASH_REG16(hw, reg) ({ \
		__typeof__(hw) _hw = (hw); \
		__typeof__(reg) _reg = (reg); \
		netman_e1000_flash_read_16((netman_e1000_t*)_hw->back, _reg); \
	})

#define E1000_WRITE_FLASH_REG16(hw, reg, value) ({ \
		__typeof__(hw) _hw = (hw); \
		__typeof__(reg) _reg = (reg); \
		netman_e1000_flash_write_16((netman_e1000_t*)_hw->back, _reg, (value)); \
	})

#define memset simple_memset
#define memcpy simple_memcpy

#endif // _E1000_OSDEP_H_
