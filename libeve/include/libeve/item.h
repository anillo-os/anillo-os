/*
 * This file is part of Anillo OS
 * Copyright (C) 2022 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _LIBEVE_ITEM_H_
#define _LIBEVE_ITEM_H_

#include <libeve/base.h>
#include <libeve/objects.h>

LIBEVE_DECLARATIONS_BEGIN;

#define LIBEVE_ITEM_CLASS(_name) LIBEVE_OBJECT_CLASS(_name)

/**
 * A callback that is invoked when the item has been fully released and is going to be destroyed.
 *
 * When this callback is invoked, the item has been fully released, so there is no way to prevent the
 * item from being destroyed. However, invoking this callback is always the first thing that an item does when
 * it is going to be destroyed. As such, some item actions might still be available while the destructor
 * is executing; this is item-specific behavior. However, after the destructor returns, the item is no longer valid
 * nor usable in any way. This is true for all items.
 *
 * When this callback is invoked, it is guaranteed that all work items that the item scheduled will have completed.
 * This implies that it is guaranteed that the item's context will not be in use by libeve and can be safely cleaned up
 * by this destructor (assuming, of course, that you have not reused the context elsewhere).
 */
typedef void (*eve_item_destructor_f)(void* context);

LIBEVE_OBJECT_CLASS(item);

void eve_item_set_destructor(eve_item_t* item, eve_item_destructor_f destructor);
void* eve_item_get_context(eve_item_t* item);

LIBEVE_DECLARATIONS_END;

#endif // _LIBEVE_ITEM_H_
