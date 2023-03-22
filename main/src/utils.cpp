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
