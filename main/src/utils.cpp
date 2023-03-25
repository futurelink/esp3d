/*
  utils.cpp - utility functions
  Part of esp3D-print

  Copyright (c) 2023 Denis Pavlov

  esp3D-print is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  esp3D-print is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the MIT License
  along with esp3D-print. If not, see <https://opensource.org/license/mit/>.
*/

#include <cstring>

short int get_x_digit(char digit) {
    if ((digit >= '0') && (digit <= '9')) return (short) (digit - '0');
    else if ((digit >= 'A') && (digit <= 'F')) return (short) (digit - 'A' + 0x9);
    else if ((digit >= 'a') && (digit <= 'f')) return (short) (digit - 'a' + 0x9);
    else return -1;
}

void url_decode(char *decoded_url, const char *url) {
    int cn = 0;
    for (int i = 0; i < strlen(url); i++) {
        if (url[i] == '%') {
            if (url[i] == 0) return;
            short d1 = get_x_digit(url[i + 1]);
            short d2 = get_x_digit(url[i + 2]);
            if ((d1 >= 0) && (d2 >= 0)) {
                decoded_url[cn++] = ((d1 << 4) | d2) & 0xff;
                i += 2;
            } else {
                decoded_url[cn++] = url[i];
            }
        } else {
            decoded_url[cn++] = url[i];
        }
    }
    decoded_url[cn] = '\0';
}
