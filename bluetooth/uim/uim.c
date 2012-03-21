/*
 *  User Mode Init manager - For shared transport
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program;if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <poll.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/types.h>
#ifdef ANDROID
#include <private/android_filesystem_config.h>
#endif

#include "uim.h"

/* Maintains the exit state of UIM*/
static int exiting;
#define UIM_DEBUG

/* Maintains the exit state of UIM*/
static int exiting;
static int line_discipline;
static int dev_fd;

/* BD address as string and a pointer to array of hex bytes */
char uim_bd_address[BD_ADDR_LEN+1];
bdaddr_t *bd_addr;

/*****************************************************************************/
#ifdef UIM_DEBUG
/*  Function to Read the firmware version
 *  module into the system. Currently used for
 *  debugging purpose, whenever the baud rate is changed
 */
void read_firmware_version(int dev_fd)
{
	int index = 0;
	char resp_buffer[20] = { 0 };
	unsigned char buffer[] = { 0x01, 0x01, 0x10, 0x00 };

	UIM_START_FUNC();
	UIM_VER(" wrote %d bytes", (int) write(dev_fd, buffer, 4));
	UIM_VER(" reading %d bytes", (int) read(dev_fd, resp_buffer, 15));

	for (index = 0; index < 15; index++)
		UIM_VER(" %x ", resp_buffer[index]);

	printf("\n");
}
#endif

/*****************************************************************************/
/* Function to read the HCI event from the given file descriptor
 *
 * This will parse the response received and returns error
 * if the required response is not received
 */
int read_hci_event(int fd, unsigned char *buf, int size)
{
	int remain, rd;
	int count = 0;
	int reading = 1;
	int rd_retry_count = 0;
	struct timespec tm = { 0, 50 * 1000 * 1000 };

	UIM_START_FUNC();

	UIM_VER(" read_hci_event");
	if (size <= 0)
		return -1;

	/* The first byte identifies the packet type. For HCI event packets, it
	 * should be 0x04, so we read until we get to the 0x04. */
	while (reading) {
		rd = read(fd, buf, 1);
		if (rd <= 0 && rd_retry_count++ < 4) {
			nanosleep(&tm, NULL);
			continue;
		} else if (rd_retry_count >= 4) {
			return -1;
		}

		if (buf[0] == RESP_PREFIX) {
			break;
		}
	}
	count++;

	/* The next two bytes are the event code and parameter total length. */
	while (count < 3) {
		rd = read(fd, buf + count, 3 - count);
		if (rd <= 0)
			return -1;
		count += rd;
	}

	/* Now we read the parameters. */
	if (buf[2] < (size - 3))
		remain = buf[2];
	else
		remain = size - 3;

	while ((count - 3) < remain) {
		rd = read(fd, buf + count, remain - (count - 3));
		if (rd <= 0)
			return -1;
		count += rd;
	}

	return count;
}

/* Function to read the Command complete event
 *
 * This will read the response for the change speed
 * command that was sent to configure the UART speed
 * with the custom baud rate
 */
static int read_command_complete(int fd, unsigned short opcode)
{
	command_complete_t resp;

	UIM_START_FUNC();

	UIM_VER(" Command complete started");
	if (read_hci_event(fd, (unsigned char *) &resp, sizeof(resp)) < 0) {
		UIM_ERR(" Invalid response");
		return -1;
	}

	/* Response should be an event packet */
	if (resp.uart_prefix != HCI_EVENT_PKT) {
		UIM_ERR
			(" Error in response: not an event packet, but 0x%02x!",
				resp.uart_prefix);
		return -1;
	}

	/* Response should be a command complete event */
	if (resp.hci_hdr.evt != EVT_CMD_COMPLETE) {
		/* event must be event-complete */
		UIM_ERR
			(" Error in response: not a cmd-complete event,but 0x%02x!",
				resp.hci_hdr.evt);
		return -1;
	}

	if (resp.hci_hdr.plen < 4) {
		/* plen >= 4 for EVT_CMD_COMPLETE */
		UIM_ERR(" Error in response: plen is not >= 4, but 0x%02x!",
				resp.hci_hdr.plen);
		return -1;
	}

	if (resp.cmd_complete.opcode != (unsigned short) opcode) {
		UIM_ERR(" Error in response: opcode is 0x%04x, not 0x%04x!",
				resp.cmd_complete.opcode, opcode);
		return -1;
	}

	UIM_DBG(" Command complete done");
	return resp.status == 0 ? 0 : -1;
}

/* Function to set the default baud rate
 *
 * The default baud rate of 115200 is set to the UART from the host side
 * by making a call to this function.This function is also called before
 * making a call to set the custom baud rate
 */
static int set_baud_rate(int dev_fd)
{
	UIM_START_FUNC();
	struct termios ti;

	tcflush(dev_fd, TCIOFLUSH);

	/* Get the attributes of UART */
	if (tcgetattr(dev_fd, &ti) < 0) {
		UIM_ERR(" Can't get port settings");
		return -1;
	}

	/* Change the UART attributes before
	 * setting the default baud rate*/
	cfmakeraw(&ti);

	ti.c_cflag |= 1;
	ti.c_cflag |= CRTSCTS;

	/* Set the attributes of UART after making
	 * the above changes
	 */
	tcsetattr(dev_fd, TCSANOW, &ti);

	/* Set the actual default baud rate */
	cfsetospeed(&ti, B115200);
	cfsetispeed(&ti, B115200);
	tcsetattr(dev_fd, TCSANOW, &ti);

	tcflush(dev_fd, TCIOFLUSH);
	UIM_DBG(" set_baud_rate() done");

	return 0;
}

/* Function to set the UART custom baud rate.
 *
 * The UART baud rate has already been
 * set to default value 115200 before calling this function.
 * The baud rate is then changed to custom baud rate by this function*/
static int set_custom_baud_rate(int dev_fd, int baud_rate, int flow_ctrl)
{
	UIM_START_FUNC();

	struct termios ti;
	struct termios2 ti2;

	/* Flush non-transmitted output data,
	 * non-read input data or both*/
	tcflush(dev_fd, TCIOFLUSH);
	/* Get the attributes of UART */
	if (tcgetattr(dev_fd, &ti) < 0) {
		UIM_ERR(" Can't get port settings");
		return -1;
	}

	/*Set the UART flow control */
	if (flow_ctrl)
		ti.c_cflag |= CRTSCTS;
	else
		ti.c_cflag &= ~CRTSCTS;

	/*
	 * Set the parameters associated with the UART
	 * The change will occur immediately by using TCSANOW
	 */
	if (tcsetattr(dev_fd, TCSANOW, &ti) < 0) {
		UIM_ERR(" Can't set port settings");
		return -1;
	}

	tcflush(dev_fd, TCIOFLUSH);

	/*Set the actual baud rate */
	ioctl(dev_fd, TCGETS2, &ti2);
	ti2.c_cflag &= ~CBAUD;
	ti2.c_cflag |= BOTHER;
	ti2.c_ospeed = baud_rate;
	ioctl(dev_fd, TCSETS2, &ti2);

	UIM_DBG(" set_custom_baud_rate() done");
	return 0;
}

/* Function to configure the UART
 * on receiving a notification from the ST KIM driver to install the line
 * discipline, this function does UART configuration necessary for the STK
 */
int st_uart_config(unsigned char install)
{
	int ldisc, len, fd, flow_ctrl;
	unsigned char buf[UART_DEV_NAME_LEN+1];
	uim_speed_change_cmd cmd;
	char uart_dev_name[UART_DEV_NAME_LEN+1];
	long cust_baud_rate;

	uim_bdaddr_change_cmd addr_cmd;

	UIM_START_FUNC();

	if (install == '1') {
		memset(buf, 0, UART_DEV_NAME_LEN+1);
		fd = open(DEV_NAME_SYSFS, O_RDONLY);
		if (fd < 0) {
			UIM_ERR("Can't open %s", DEV_NAME_SYSFS);
			return -1;
		}
		len = read(fd, buf, UART_DEV_NAME_LEN);
		if (len < 0) {
			UIM_ERR("read err (%s)", strerror(errno));
			close(fd);
			return len;
		}
		sscanf((const char *) buf, "%s", uart_dev_name);
		close(fd);

		memset(buf, 0, UART_DEV_NAME_LEN+1);
		fd = open(BAUD_RATE_SYSFS, O_RDONLY);
		if (fd < 0) {
			UIM_ERR("Can't open %s", BAUD_RATE_SYSFS);
			return -1;
		}
		len = read(fd, buf, UART_DEV_NAME_LEN);
		if (len < 0) {
			UIM_ERR("read err (%s)", strerror(errno));
			close(fd);
			return len;
		}
		close(fd);
		sscanf((const char *) buf, "%ld", &cust_baud_rate);

		memset(buf, 0, UART_DEV_NAME_LEN+1);
		fd = open(FLOW_CTRL_SYSFS, O_RDONLY);
		if (fd < 0) {
			UIM_ERR("Can't open %s", FLOW_CTRL_SYSFS);
			/* As fd was not opened, it's not necessary to close it */
			return -1;
		}
		len = read(fd, buf, UART_DEV_NAME_LEN);
		if (len < 0) {
			UIM_ERR("read err (%s)", strerror(errno));
			close(fd);
			return len;
		}
		close(fd);
		sscanf((const char *) buf, "%d", &flow_ctrl);

		UIM_VER(" signal received, opening %s", uart_dev_name);

		dev_fd = open(uart_dev_name, O_RDWR);
		if (dev_fd < 0) {
			UIM_ERR("Can't open %s", uart_dev_name);
			return -1;
		}

		UIM_VER(" Setting default baudrate");

		/*
		 * Set only the default baud rate.
		 * This will set the baud rate to default 115200
		 */
		if (set_baud_rate(dev_fd) < 0) {
			UIM_ERR("set_baudrate() failed");
			close(dev_fd);
			return -1;
		}

		fcntl(dev_fd, F_SETFL, fcntl(dev_fd, F_GETFL) | O_NONBLOCK);
		/* Set only the custom baud rate */
		if (cust_baud_rate != 115200) {

			UIM_VER("Setting speed to %ld", cust_baud_rate);
			/* Forming the packet for Change speed command */
			cmd.uart_prefix = HCI_COMMAND_PKT;
			cmd.hci_hdr.opcode = HCI_HDR_OPCODE;
			cmd.hci_hdr.plen = sizeof(unsigned long);
			cmd.speed = cust_baud_rate;

			/* Writing the change speed command to the UART
			 * This will change the UART speed at the controller
			 * side
			 */
			UIM_VER(" Setting speed to %d", cust_baud_rate);
			len = write(dev_fd, &cmd, sizeof(cmd));
			if (len < 0) {
				UIM_ERR("Failed to write speed-set command");
				close(dev_fd);
				return -1;
			}

			/* Read the response for the Change speed command */
			if (read_command_complete(dev_fd, HCI_HDR_OPCODE) < 0) {
				close(dev_fd);
				return -1;
			}

			UIM_VER(" Speed changed to %d", cust_baud_rate);

			/* Set the actual custom baud rate at the host side */
			if (set_custom_baud_rate(dev_fd, cust_baud_rate, flow_ctrl) < 0) {
				UIM_ERR("set_custom_baud_rate() failed");
				close(dev_fd);

				return -1;
			}

			/* Set the uim BD address */
			if (bd_addr) {

				memset(&addr_cmd, 0, sizeof(addr_cmd));
				/* Forming the packet for change BD address command*/
				addr_cmd.uart_prefix = HCI_COMMAND_PKT;
				addr_cmd.hci_hdr.opcode = WRITE_BD_ADDR_OPCODE;
				addr_cmd.hci_hdr.plen = sizeof(bdaddr_t);
				memcpy(&addr_cmd.addr, bd_addr, sizeof(bdaddr_t));

				/* Writing the change BD address command to the UART
				 * This will change the change BD address  at the controller
				 * side
				 */
				len = write(dev_fd, &addr_cmd, sizeof(addr_cmd));
				if (len < 0) {
					UIM_ERR("Failed to write BD address command");
					close(dev_fd);
					return -1;
				}

				/* Read the response for the change BD address command */
				if (read_command_complete(dev_fd, WRITE_BD_ADDR_OPCODE) < 0) {
					close(dev_fd);
					return -1;
				}
				UIM_VER("BD address changed to "
						"%02X:%02X:%02X:%02X:%02X:%02X", bd_addr->b[0],
						bd_addr->b[1], bd_addr->b[2], bd_addr->b[3],
						bd_addr->b[4], bd_addr->b[5]);
			}
#ifdef UIM_DEBUG
			read_firmware_version(dev_fd);
#endif
		}

		/* After the UART speed has been changed, the IOCTL is
		 * is called to set the line discipline to N_TI_WL
		 */
		ldisc = N_TI_WL;
		if (ioctl(dev_fd, TIOCSETD, &ldisc) < 0) {
			UIM_ERR(" Can't set line discipline");
			close(dev_fd);
			return -1;
		}
		UIM_DBG("Installed N_TI_WL Line displine");
	} else {
		UIM_DBG("Un-Installed N_TI_WL Line displine");
		/* UNINSTALL_N_TI_WL - When the Signal is received from KIM */
		/* closing UART fd */
		close(dev_fd);
	}
	return 0;
}

/* Function to convert the BD address from ascii to hex value */
bdaddr_t *strtoba(const char *str)
{
	uint8_t *ba = malloc(sizeof(bdaddr_t));
	unsigned int tmp_bd[BD_ADDR_BIN_LEN];
	int i;


	if (ba) {
		memset(tmp_bd, 0, BD_ADDR_BIN_LEN);
		if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
				&tmp_bd[0], &tmp_bd[1], &tmp_bd[2],
				&tmp_bd[3], &tmp_bd[4], &tmp_bd[5]) != sizeof(bdaddr_t)) {
			free (ba);
			ba = NULL;
			goto exit;
		}
		for (i=0;i<BD_ADDR_BIN_LEN;i++){
			if(tmp_bd[i] > 255){
				free (ba);
				ba = NULL;
				goto exit;
			}
			ba[i] = (uint8_t) tmp_bd[i];
		}

	}
exit:
	return (bdaddr_t *) ba;
}

/*****************************************************************************/
int main(int argc, char *argv[])
{
	int st_fd, err;
	unsigned char install, previous;
	struct pollfd p;
	unsigned int i;
	/* List of invalid BD addresses */
	const bdaddr_t bd_address_ignored[] = {
			{ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
			{ { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } } };

	UIM_START_FUNC();
	bd_addr = NULL;
	err = 0;

	/* Parse the user input */
	if ((argc > 2)) {
		UIM_ERR("Invalid arguments");
		UIM_ERR("Usage: uim <bd address>");
		return -1;
	}
	if (argc == 2) {
		if (strlen(argv[1]) != BD_ADDR_LEN) {
			UIM_ERR("Usage: uim XX:XX:XX:XX:XX:XX");
			return -1;
		}
		/* BD address passed as string in xx:xx:xx:xx:xx:xx format */
		strncpy(uim_bd_address, argv[1], BD_ADDR_LEN);
		/* ensure that null terminated is correctly set at end of buf */
		uim_bd_address[BD_ADDR_LEN]='\0';
		bd_addr = strtoba(uim_bd_address);
	}

	if (bd_addr) {
		/* Check if read value has to be ignored */
		for (i = 0; i < (sizeof(bd_address_ignored) / sizeof(bdaddr_t)); i++) {

			if (memcmp(&bd_address_ignored[i], bd_addr, sizeof(bdaddr_t)) == 0) {

				UIM_DBG("Stored value "
						"%02X:%02X:%02X:%02X:%02X:%02X was ignored",
						bd_addr->b[0], bd_addr->b[1], bd_addr->b[2],
						bd_addr->b[3], bd_addr->b[4], bd_addr->b[5]);
				UIM_DBG("Using default chip bd address");

				free(bd_addr);
				bd_addr = NULL;

				break;
			}
		}
		if (bd_addr)
			UIM_DBG("Using %s bd address", uim_bd_address);
	} else
		UIM_DBG("Using default chip bd address");

	line_discipline = N_TI_WL;

	st_fd = open(INSTALL_SYSFS_ENTRY, O_RDONLY);
	if (st_fd < 0) {
		UIM_DBG("unable to open %s(%s)", INSTALL_SYSFS_ENTRY, strerror(errno));
		return -1;
	}

	/* read to start proper poll */
	err = read(st_fd, &install, 1);
	/* special case where bluetoothd starts before the UIM, and UIM
	 * needs to turn on bluetooth because of that.
	 */
	if ((err > 0) && install == '1') {
		UIM_DBG("install set previously...");
		st_uart_config(install);
	}

RE_POLL:

	UIM_DBG("begin polling...");

	memset(&p, 0, sizeof(p));
	p.fd = st_fd;
	p.events = POLLERR | POLLPRI;

	while (!exiting) {
		p.revents = 0;
		err = poll(&p, 1, -1);
		UIM_DBG("poll broke due to event %d(PRI:%d/ERR:%d)\n", p.revents, POLLPRI, POLLERR);
		if (err < 0 && errno == EINTR)
			continue;
		if (err)
			break;
	}

	close(st_fd);
	st_fd = open(INSTALL_SYSFS_ENTRY, O_RDONLY);
	if (st_fd < 0) {
		UIM_DBG("unable to open %s (%s)", INSTALL_SYSFS_ENTRY, strerror(errno));
		return -1;
	}

	if (!exiting) {
		previous = install;
		err = read(st_fd, &install, 1);
		UIM_DBG("read %c from install (previously was %c)\n", install, previous);
		if (err > 0)
			if (previous != install)
				st_uart_config(install);
			else
				UIM_DBG("lost install event, retry later");

		goto RE_POLL;
	}

	close(st_fd);
	/* Free resources */
	if (bd_addr)
		free(bd_addr);
	return 0;
}
