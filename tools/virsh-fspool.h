/*
 * virsh-fspool.h: Commands to manage fspool
 *
 * Copyright (C) 2016 Parallels IP Holdings GmbH
 *
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
 *
 */

#ifndef VIRSH_FSPOOL_H
# define VIRSH_FSPOOL_H

# include "virsh.h"

virFSPoolPtr
virshCommandOptFSPoolBy(vshControl *ctl, const vshCmd *cmd, const char *optname,
                      const char **name, unsigned int flags);

/* default is lookup by Name and UUID */
# define virshCommandOptFSPool(_ctl, _cmd, _optname, _name)           \
    virshCommandOptFSPoolBy(_ctl, _cmd, _optname, _name,              \
                            VIRSH_BYUUID | VIRSH_BYNAME)

extern const vshCmdDef fsPoolCmds[];

#endif /* VIRSH_FSPOOL_H */
