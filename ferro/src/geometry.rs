use core::ops::Add;

#[derive(Default, Clone, Copy)]
pub struct Point {
	pub x: usize,
	pub y: usize,
}

impl Point {
	pub const fn new(x: usize, y: usize) -> Self {
		Self { x, y }
	}

	pub const fn is_within_rect(&self, rect: &Rect) -> bool {
		self.x >= rect.origin.x &&
		self.y >= rect.origin.y &&
		self.x < rect.origin.x + rect.size.width &&
		self.y < rect.origin.y + rect.size.height
	}

	pub const fn as_index(&self, width: usize) -> usize {
		(self.y * width) + self.x
	}

	/// Retrieves the next point within the given bounds (if there is one).
	/// Point order goes from left to right then top to bottom.
	///
	/// Formally, this means that the next point is the one with the same Y-coordinate but an X-coordinate
	/// greater by one, if this is the last point in a row, a point with an X-coordinate of 0 and a
	/// Y-coordinate greater by one.
	pub const fn next_point(&self, bounds: &Size2D) -> Option<Self> {
		if self.y == bounds.width - 1 && self.x == bounds.height - 1 {
			None
		} else {
			Some(Self::new(if self.x == bounds.width - 1 { 0 } else { self.x + 1 }, if self.x == bounds.width - 1 { self.y + 1 } else { self.y }))
		}
	}
}

impl const Add for &Point {
	type Output = Point;

	fn add(self, rhs: Self) -> Self::Output {
		Point::new(self.x + rhs.x, self.y + rhs.y)
	}
}

impl const Add<&Size2D> for &Point {
	type Output = Point;

	fn add(self, rhs: &Size2D) -> Self::Output {
		Point::new(self.x + rhs.width, self.y + rhs.height)
	}
}

impl const From<(usize, usize)> for Point {
	fn from(value: (usize, usize)) -> Self {
		Self::new(value.0, value.1)
	}
}

#[derive(Default, Clone, Copy)]
pub struct Size2D {
	pub width: usize,
	pub height: usize,
}

impl Size2D {
	pub const fn new(width: usize, height: usize) -> Self {
		Self { width, height }
	}
}

impl const From<(usize, usize)> for Size2D {
	fn from(value: (usize, usize)) -> Self {
		Self::new(value.0, value.1)
	}
}

#[derive(Default, Clone, Copy)]
pub struct Rect {
	/// top-left corner
	pub origin: Point,
	pub size: Size2D,
}

impl Rect {
	pub const fn new(origin: Point, size: Size2D) -> Self {
		Self { origin, size }
	}

	pub const fn new_from_points(first: Point, second: Point) -> Self {
		let width = (if first.x < second.x { second.x - first.x } else { first.x - second.x }) + 1;
		let height = (if first.y < second.y { second.y - first.y } else { first.y - second.y }) + 1;
		let x = if first.x < second.x { first.x } else { second.x };
		let y = if first.y < second.y { first.y } else { second.y };
		Self::new(Point::new(x, y), Size2D::new(width, height))
	}

	pub const fn top_left(&self) -> Point {
		self.origin
	}

	pub const fn top_right(&self) -> Point {
		Point::new(self.origin.x + self.size.width - 1, self.origin.y)
	}

	pub const fn bottom_left(&self) -> Point {
		Point::new(self.origin.x, self.origin.y + self.size.height - 1)
	}

	pub const fn bottom_right(&self) -> Point {
		Point::new(self.origin.x + self.size.width - 1, self.origin.y + self.size.height - 1)
	}

	pub const fn is_within_rect(&self, containing_rect: &Self) -> bool {
		self.top_left().is_within_rect(containing_rect) && self.bottom_right().is_within_rect(containing_rect)
	}
}
