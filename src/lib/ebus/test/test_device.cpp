/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2016 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "device.h"
#include <iostream>
#include <iomanip>

using namespace std;

int main ()
{
	auto device = Device::create("/dev/ttyUSB20", true, false, false, NULL);
	if (device == NULL) {
		cout << "unable to create device" << endl;
		return -1;
	}
	result_t result = device->open();
	if (result != RESULT_OK) {
		cout << "open failed: " << getResultCode(result) << endl;
	} else {
		if (!device->isValid())
			cout << "device not available." << endl;

		int count = 0;

		while (1) {
			unsigned char byte = 0;
			result = device->recv(0, byte);

			if (result == RESULT_OK)
				cout << hex << setw(2) << setfill('0')
				<< static_cast<unsigned>(byte) << endl;

			count++;
		}

		device->close();

		if (!device->isValid())
			cout << "close successful." << endl;
	}

	return 0;
}
