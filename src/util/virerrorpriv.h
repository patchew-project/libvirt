/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __VIR_ERROR_ALLOW_INCLUDE_PRIV_H__
# error "virerrorpriv.h may only be included by virerror.c or its test suite"
#endif

#ifndef __VIR_ERROR_PRIV_H__
# define __VIR_ERROR_PRIV_H__

const char *
virErrorMsg(virErrorNumber error,
            const char *info);

#endif /* __VIR_ERROR_PRIV_H__ */
