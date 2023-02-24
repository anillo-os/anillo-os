mod font;

use core::fmt::{Arguments, Result, write, Write, Error};

use crate::sync::{SpinLock, Lock};

struct Console {

}

impl Console {
	const fn new() -> Self {
		Self {}
	}
}

impl Write for Console {
	fn write_str(&mut self, string: &str) -> Result {
		Err(Error)
	}
}

static CONSOLE: SpinLock<Console> = SpinLock::new(Console::new());

#[macro_export]
macro_rules! kprint {
	($($arg:tt)*) => {{
		$crate::console::kprint_args(::core::format_args!($($arg)*)).unwrap();
	}};
}

#[macro_export]
macro_rules! kprintln {
	() => {
		$crate::kprint!("\n")
	};
	($($arg:tt)*) => {{
		$crate::console::kprint_args(::core::format_args_nl!($($arg)*)).unwrap();
	}};
}

pub fn kprint_args(args: Arguments) -> Result {
	let mut console = CONSOLE.lock();
	write(&mut *console, args)
}
