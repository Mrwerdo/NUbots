/*
 * This file is part of the NUbots Codebase.
 *
 * The NUbots Codebase is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The NUbots Codebase is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the NUbots Codebase.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2013 NUbots <nubots@nubots.net>
 */

#include "DarwinDevice.hpp"

namespace Darwin {
    DarwinDevice::DarwinDevice(UART& coms, int id) : coms(coms), id(id) {}

    bool DarwinDevice::ping() {

        // Ping and get the result
        CommandResult result = coms.executeRead(PingCommand(id));

        // Check if there was an error code
        return result.header.errorcode == ErrorCode::NONE;
    }
}  // namespace Darwin
