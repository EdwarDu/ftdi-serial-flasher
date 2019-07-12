#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "ftdi.h"
#include <libusb.h>

int main(int argc, char* argv[]) {
    int opt;
    struct ftdi_context *ftdi = NULL;
    int vid_i = 0;
    int pid_i = 0;
    char *serial_number = NULL;
    char *eeprom_load_filename = NULL;
    char *eeprom_dump_filename = NULL;

    while ((opt = getopt(argc, argv, "v:p:n:L:D:")) != -1){
        switch(opt) {
            case 'v':
                vid_i = (int) strtol(optarg, NULL, 0);
                break;
            case 'p':
                pid_i = (int) strtol(optarg, NULL, 0);
                break;
            case 'n':
                serial_number = optarg;
                break;
            case 'L':
                eeprom_load_filename = optarg;
                break;
            case 'D':
                eeprom_dump_filename = optarg;
                break;
            default:
                fprintf(stderr, "usage: %s [options]\n", *argv);
                fprintf(stderr, "\t-v <hex number> Search for device with VID == number\n");
                fprintf(stderr, "\t-p <hex number> Search for device with PID == number\n");
                fprintf(stderr, "\t-n <string? New serial number (<= 8 char)\n");
                fprintf(stderr, "\t-L <string? filename to load eeprom from, "
                                "otherwise, eeprom from devcie with be used\n");
                fprintf(stderr, "\t-D <string? filename to dump eeprom to\n");
                return -1;
        }
    }


    unsigned char eeprom_buf[256];

    ftdi = ftdi_new();
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
        fprintf(stderr, "No device VID:%04X, PID:%04X found\n", vid_i, pid_i);
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

            if (eeprom_dump_filename != NULL){
                FILE* f_eeprom = fopen(eeprom_dump_filename, "wb");
                if (f_eeprom == NULL){
                    fprintf(stderr, "Failed to open EEPROM file %s to dump\n", eeprom_dump_filename);
                }
                else {
                   res = fwrite(eeprom_buf, 1, eeprom_size, f_eeprom);
                    if (res != eeprom_size) {
                        fprintf(stderr, "Failed to dump to EEPROM file %s: read %d instead of %d\n",
                                eeprom_dump_filename, res, eeprom_size);
                    } else {
                        fprintf(stderr, "EEPROM file %s dump ok\n", eeprom_dump_filename);
                    }
                    fclose(f_eeprom);
                }
            }

            if (eeprom_load_filename != NULL){
                FILE* f_eeprom = fopen(eeprom_load_filename, "rb");
                if (f_eeprom == NULL){
                    fprintf(stderr, "Failed to load EEPROM file %s\n", eeprom_load_filename);
                }
                else {
                    long f_size = 0;
                    fseek(f_eeprom, 0, SEEK_END);
                    f_size = ftell(f_eeprom);
                    if (f_size != eeprom_size) {
                        fprintf(stderr, "EEPROM file %s size incorrect: %ld instead of %d\n",
                                eeprom_load_filename, f_size, eeprom_size);
                    }
                    else {
                        rewind(f_eeprom);
                        unsigned char* temp_buf = (unsigned char*) malloc(eeprom_size);
                        res = fread(temp_buf, 1, eeprom_size, f_eeprom);
                        if (res != eeprom_size) {
                            fprintf(stderr, "Failed to read from EEPROM file %s: read %d instead of %d\n",
                                    eeprom_load_filename, res, eeprom_size);
                        } else {
                            memcpy(eeprom_buf, temp_buf, eeprom_size);
                            fprintf(stderr, "EEPROM file %s loaded\n", eeprom_load_filename);
                        }
                        free(temp_buf);
                    }
                    fclose(f_eeprom);
                }
            }

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
                    continue;

                fprintf(stderr, "Starting to write to EEPROM\n");

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
                        break;
                    }
                }

                fprintf(stderr, "Write to EEPROM OKAY\n");
            }
        }
        ftdi_usb_close(ftdi);
    }

    ftdi_list_free(&devlist);
    ftdi_free(ftdi);
    return 0;
}