/*
 * Copyright (c) 2018-2020 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdarg.h>
#include "utils.h"
#include "se.h"
#include <inttypes.h>

__attribute__ ((noreturn)) void generic_panic(void) {
    /* Clear keyslots. */
    clear_aes_keyslot(0xD);
    clear_aes_keyslot(0xE);
    for (size_t i = 0; i < 0x10; i++) {
        clear_aes_keyslot(i);
    }
    clear_aes_keyslot(0xD);
    clear_aes_keyslot(0xE);
    while(1) { /* ... */ }
}
