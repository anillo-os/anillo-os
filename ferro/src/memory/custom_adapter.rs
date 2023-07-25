/*
 * Copyright 2016 Amanieu d'Antras
 * Copyright 2020 Amari Robinson
 * Copyright (C) 2023 Anillo OS Developers
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

/// This macro extends the `intrusive_adapter` macro provided by the `intrusive_collections` crate
/// to allow using custom pointer ops.
///
/// (That's why the copyright for this file is actually that of `src/adapter.rs` within the `intrusive_collections` crate
/// with an additional copyright for the Anillo OS developers due to our modifications).
#[macro_export]
macro_rules! custom_intrusive_adapter {
	(@impl
		$(#[$attr:meta])* $vis:vis $name:ident ($($args:tt),*)
		= $pointer_ops:ty: $value:path { $field:ident: $link:ty } $($where_:tt)*
	) => {
		#[allow(explicit_outlives_requirements)]
		$(#[$attr])*
		$vis struct $name<$($args),*> $($where_)* {
			link_ops: <$link as ::intrusive_collections::DefaultLinkOps>::Ops,
			pointer_ops: $pointer_ops,
		}
		unsafe impl<$($args),*> Send for $name<$($args),*> $($where_)* {}
		unsafe impl<$($args),*> Sync for $name<$($args),*> $($where_)* {}
		impl<$($args),*> Copy for $name<$($args),*> $($where_)* {}
		impl<$($args),*> Clone for $name<$($args),*> $($where_)* {
			#[inline]
			fn clone(&self) -> Self {
				*self
			}
		}
		impl<$($args),*> Default for $name<$($args),*> $($where_)* {
			#[inline]
			fn default() -> Self {
				Self::NEW
			}
		}
		#[allow(dead_code)]
		impl<$($args),*> $name<$($args),*> $($where_)* {
			pub const NEW: Self = $name {
				link_ops: <$link as ::intrusive_collections::DefaultLinkOps>::NEW,
				pointer_ops: <$pointer_ops>::new(),
			};
			#[inline]
			pub fn new() -> Self {
				Self::NEW
			}
		}
		#[allow(dead_code, unsafe_code)]
		unsafe impl<$($args),*> ::intrusive_collections::Adapter for $name<$($args),*> $($where_)* {
			type LinkOps = <$link as ::intrusive_collections::DefaultLinkOps>::Ops;
			type PointerOps = $pointer_ops;

			#[inline]
			unsafe fn get_value(&self, link: <Self::LinkOps as ::intrusive_collections::LinkOps>::LinkPtr) -> *const <Self::PointerOps as ::intrusive_collections::PointerOps>::Value {
				::intrusive_collections::container_of!(link.as_ptr(), $value, $field)
			}
			#[inline]
			unsafe fn get_link(&self, value: *const <Self::PointerOps as ::intrusive_collections::PointerOps>::Value) -> <Self::LinkOps as ::intrusive_collections::LinkOps>::LinkPtr {
				// We need to do this instead of just accessing the field directly
				// to strictly follow the stack borrow rules.
				let ptr = (value as *const u8).add(::intrusive_collections::offset_of!($value, $field));
				core::ptr::NonNull::new_unchecked(ptr as *mut _)
			}
			#[inline]
			fn link_ops(&self) -> &Self::LinkOps {
				&self.link_ops
			}
			#[inline]
			fn link_ops_mut(&mut self) -> &mut Self::LinkOps {
				&mut self.link_ops
			}
			#[inline]
			fn pointer_ops(&self) -> &Self::PointerOps {
				&self.pointer_ops
			}
		}
	};
	(@find_generic
		$(#[$attr:meta])* $vis:vis $name:ident ($($prev:tt)*) > $($rest:tt)*
	) => {
		custom_intrusive_adapter!(@impl
			$(#[$attr])* $vis $name ($($prev)*) $($rest)*
		);
	};
	(@find_generic
		$(#[$attr:meta])* $vis:vis $name:ident ($($prev:tt)*) $cur:tt $($rest:tt)*
	) => {
		custom_intrusive_adapter!(@find_generic
			$(#[$attr])* $vis $name ($($prev)* $cur) $($rest)*
		);
	};
	(@find_if_generic
		$(#[$attr:meta])* $vis:vis $name:ident < $($rest:tt)*
	) => {
		custom_intrusive_adapter!(@find_generic
			$(#[$attr])* $vis $name () $($rest)*
		);
	};
	(@find_if_generic
		$(#[$attr:meta])* $vis:vis $name:ident $($rest:tt)*
	) => {
		custom_intrusive_adapter!(@impl
			$(#[$attr])* $vis $name () $($rest)*
		);
	};
	($(#[$attr:meta])* $vis:vis $name:ident $($rest:tt)*) => {
		custom_intrusive_adapter!(@find_if_generic
			$(#[$attr])* $vis $name $($rest)*
		);
	};
}
