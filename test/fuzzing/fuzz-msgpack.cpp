/*
 *  Copyright (C) 2021-2023 Savoir-faire Linux Inc.
 *
 *  Author: Olivier Dion <olivier.dion>@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 */

#include "lib/gnutls.h"
#include "lib/syslog.h"

/*
 * Reverse channel and data in packed message
 */
void
pack_gnutls_record_recv(msgpack::sbuffer& buf, const ChanneledMessage& msg)
{
     msgpack::packer<msgpack::sbuffer> pk(&buf);

     pk.pack_array(2);
     pk.pack_bin(msg.data.size());
     pk.pack_bin_body((const char*) msg.data.data(), msg.data.size());
     pk.pack(msg.channel);
}

bool
mutate_gnutls_record_recv(ChanneledMessage& msg)
{
    (void)msg;

    return true;
}


#include "scenarios/classic-alice-and-bob.h"
