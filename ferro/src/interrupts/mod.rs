//! A subsystem for managing interrupts in the kernel.
//!
//! # Safety
//!
//! Great care must be taken with interrupt code, as it can be executed at any time.
//!
//! For example: it's very important for interrupt handlers not to access data that isn't protected by a lock.
//! Such data may already be in-use by the code that was just interrupted; if so, accessing it in the interrupt
//! handler would violate Rust's aliasing rules.

mod common;

#[cfg(target_arch = "x86_64")]
mod x86_64;
#[cfg(target_arch = "x86_64")]
use x86_64 as arch;

#[cfg(target_arch = "aarch64")]
mod aarch64;
#[cfg(target_arch = "aarch64")]
use aarch64 as arch;
pub use arch::*;
