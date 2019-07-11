#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ftdi.h"
#include <libusb.h>

int main(int argc, char* argv[]) {
    struct ftdi_context *ftdi = ftdi_new();
    char* vid = argv[1];
    char* pid = argv[2];
    int vid_i = (int) strtol(vid, NULL, 0);
    int pid_i = (int) strtol(pid, NULL, 0);

    char* serial_number = argc > 3 ? argv[3] : NULL;
    unsigned char eeprom_buf[256];

    if (ftdi == NULL){
        fprintf(stderr, "Failed to allocate ftdi structure: %s\n",
                ftdi_get_error_string(ftdi));
        return -1;
    }

    ftdi_set_interface(ftdi, INTERFACE_ANY);
    int res = 0;
    int dev_i = 0;
    struct ftdi_device_list *devlist, *curdev;
    res = ftdi_usb_find_all(ftdi, &devlist, vid_i, pid_i);
    if (res < 0){
        ftdi_free(ftdi);
        fprintf(stderr, "No device VID:%s, PID:%s found\n", vid, pid);
        return -1;
    }
    for (curdev = devlist; curdev != NULL; curdev = curdev->next, dev_i++){
        res = ftdi_usb_open_dev(ftdi, curdev->dev);
        if (res < 0) {
            fprintf(stderr, "Unable to open device(%d): %s\n", dev_i, ftdi_get_error_string(ftdi));
            continue;
        }
        fprintf(stderr, "--------------Decoding the EEPROM for device(%d)-------------\n", dev_i);
        int value;
        res = ftdi_read_eeprom(ftdi);
        if (res < 0){
            fprintf(stderr, "Failed to read EEPROM: %s\n", ftdi_get_error_string(ftdi));
            continue;
        }
        int eeprom_size;
        ftdi_get_eeprom_value(ftdi, CHIP_SIZE, &value);
        if (value < 0){
            fprintf(stderr, "No EEPROM found or EEPROM empty\n");
            fprintf(stderr, "On empty EEPROM, use -w option to write default values\n");
            continue;
        }
        fprintf(stderr, "Chip type %d ftdi_eeprom_size: %d\n", ftdi->type, value);
        if (ftdi->type == TYPE_R) eeprom_size = 0xa0;
        else eeprom_size = value;

        ftdi_get_eeprom_buf(ftdi, eeprom_buf, eeprom_size);

        res = ftdi_eeprom_decode(ftdi, 1);
        if (res != 0){
            fprintf(stderr, "Failed to decode eeprom: %s\n", ftdi_get_error_string(ftdi));
            continue;
        }

        // Addr 0E: Offset of the manufacturer string + 0x80, calculated later
        // Addr 0F: Length of manufacturer string
        int str_value_size = eeprom_buf[0x0F] / 2;
        int i = 0, j = 0;
        if (str_value_size > 0){
            char* str_value = (char*) malloc(str_value_size);
            i = eeprom_buf[0x0E]  & (eeprom_size - 1);
            for (j=0; j<str_value_size-1; j++){
                str_value[j] = eeprom_buf[2*j + i + 2];
            }
            str_value[j] = '\0';
            fprintf(stderr, "Manufacturer: %s\n", str_value);
            free(str_value);
        }

        str_value_size = eeprom_buf[0x11] / 2;
        if (str_value_size > 0){
            char* str_value = (char*) malloc(str_value_size);
            i = eeprom_buf[0x10]  & (eeprom_size - 1);
            for (j=0; j<str_value_size-1; j++){
                str_value[j] = eeprom_buf[2*j + i + 2];
            }
            str_value[j] = '\0';
            fprintf(stderr, "Product: %s\n", str_value);
            free(str_value);
        }

        str_value_size = eeprom_buf[0x13] / 2;
        if (str_value_size > 0){
            char* str_value = (char*) malloc(str_value_size);
            i = eeprom_buf[0x12]  & (eeprom_size - 1);
            for (j=0; j<str_value_size-1; j++){
                str_value[j] = eeprom_buf[2*j + i + 2];
            }
            str_value[j] = '\0';
            fprintf(stderr, "Serial: %s\n", str_value);
            free(str_value);

            unsigned short checksum = 0xAAAA;
            for (i = 0; i < eeprom_size/2-1; i ++){
                if ((ftdi->type == TYPE_230X) && (i == 0x12))
                {
                    /* FT230X has a user section in the MTP which is not part of the checksum */
                    i = 0x40;
                }
                checksum ^= ((unsigned short)eeprom_buf[i*2] + ((unsigned short)eeprom_buf[(i*2)+1] << 8));
                checksum = (checksum << 1) | (checksum >> 15);
            }

            fprintf(stderr, "Checksum %02X %02X ->> %02X %02X\n",
                    (checksum >> 8) & 0xFF,
                    checksum & 0xFF,
                    eeprom_buf[eeprom_size-1],
                    eeprom_buf[eeprom_size-2]);

            if (serial_number != NULL) {
                int serial_number_len = strlen(serial_number);
                serial_number_len = serial_number_len > 20 ? 20 : serial_number_len;
                str_value = (char *) malloc(serial_number_len + 1);
                strncpy(str_value, serial_number, serial_number_len);
                printf("New Serial: %s\n", str_value);
                i = (eeprom_buf[0x12] & (eeprom_size - 1)) + 2;
                eeprom_buf[0x13] = serial_number_len * 2 + 2;
                for (j = 0; j < serial_number_len; j++) {
                    eeprom_buf[i] = str_value[j];
                    i++;
                    eeprom_buf[i] = 0x00;
                    i++;
                }

                if (ftdi->type > TYPE_BM) {
                    eeprom_buf[i & (eeprom_size - 1)] = 0x02;
                    i++;
                    eeprom_buf[i & (eeprom_size - 1)] = 0x03;
                    i++;
                    eeprom_buf[i & (eeprom_size - 1)] = 0x00; // FIXME : not checked
                    i++;
                }

                free(str_value);

                checksum = 0xAAAA;
                for (i = 0; i < eeprom_size / 2 - 1; i++) {
                    checksum ^= eeprom_buf[i * 2] + ((unsigned short) eeprom_buf[(i * 2) + 1] << 8);
                    checksum = (checksum << 1) | (checksum >> 15);
                }

                eeprom_buf[eeprom_size - 2] = checksum & 0xFF;
                eeprom_buf[eeprom_size - 1] = (checksum >> 8) & 0xFF;

                res = ftdi_set_eeprom_buf(ftdi, eeprom_buf, eeprom_size);
                if (res != 0){
                    fprintf(stderr, "Failed to set EEPROM buf: %s\n", ftdi_get_error_string(ftdi));
                    continue;
                }

                unsigned short usb_val, status;
                if ((res = ftdi_usb_reset(ftdi)) != 0)
                    continue;
                if ((res = ftdi_poll_modem_status(ftdi, &status)) != 0)
                    continue;
                if ((res = ftdi_set_latency_timer(ftdi, 0x77)) != 0)
                    return res;

                fprintf(stderr, "Starting to writ to EEPROM\n");

                for (i=0; i < eeprom_size/2 ;i++){
                    /* Do not try to write to reserved area */
                    if ((ftdi->type == TYPE_230X) && (i == 0x40))
                    {
                        i = 0x50;
                    }
                    usb_val = eeprom_buf[i*2];
                    usb_val += eeprom_buf[(i*2)+1] << 8;
                    if (libusb_control_transfer(ftdi->usb_dev, FTDI_DEVICE_OUT_REQTYPE,
                                                SIO_WRITE_EEPROM_REQUEST, usb_val, i,
                                                NULL, 0, ftdi->usb_write_timeout) < 0) {
                        fprintf(stderr, "Failed to write EEPROM\n");
                        continue;
                    }
                }

                if (res != 0){
                    fprintf(stderr, "Failed to write EEPROM: %s\n", ftdi_get_error_string(ftdi));
                    continue;
                }
            }
        }
        ftdi_usb_close(ftdi);
    }

    ftdi_list_free(&devlist);
    ftdi_free(ftdi);
    return 0;
}