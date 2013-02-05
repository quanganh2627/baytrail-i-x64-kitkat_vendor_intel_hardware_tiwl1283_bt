/*
 *  bd_provisioning.c - bluetooth device provisioning application
 *
 *  Copyright(c) 2009-2011 Intel Corporation. All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#undef LOGV
#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <cutils/properties.h>

#ifdef BUILD_WITH_CHAABI_SUPPORT
#include "umip_access.h"
#endif

#define LOG_TAG "bd_prov"

#define BD_ADDRESS_LEN 6

#define NO_ERR                       0
#define ERR_WRONG_PARAM             -1

#define BD_ADDR_FILE_NAME	"/factory/bt/bd_addr.conf"
#define BD_LEN			18

int main(int argc, char **argv)
{
	unsigned char *chaabi_bd_address = NULL;
	int res = NO_ERR;
	char state[PROPERTY_VALUE_MAX];
	FILE *bd_addr_file = NULL;
	char *bd_addr_file_name = BD_ADDR_FILE_NAME;
	char bd_address[BD_LEN] = "00:00:00:00:00:00";

	/* Check parameters */
	if (argc != 1) {
		/* No param expected */
		return ERR_WRONG_PARAM;
	}

#ifdef BUILD_WITH_CHAABI_SUPPORT
	/* Read BD address from Chaabi */

	LOGV("Retrieving bd address from chaabi ...");

	res = get_customer_data(ACD_BT_MAC_ADDR_FIELD_INDEX,
			(void ** const) &chaabi_bd_address);
	if ((res != BD_ADDRESS_LEN) || !chaabi_bd_address) {
		/* chaabi read error OR no chaabi support */
		if (res < 0)
			LOGE("Error retrieving chaabi bd address, "
					"error %d", res);
		else
			LOGE("Error retrieving chaabi bd address, "
					"wrong length");
		if (chaabi_bd_address) {
			free(chaabi_bd_address);
			chaabi_bd_address = NULL;
		}
	} else {
		LOGV("Bd address successfully retrieved from chaabi: "
				"%02X:%02X:%02X:%02X:%02X:%02X",
				chaabi_bd_address[0], chaabi_bd_address[1],
				chaabi_bd_address[2], chaabi_bd_address[3],
				chaabi_bd_address[4], chaabi_bd_address[5]);
	}
#else
	LOGE("Chaabi not supported, "
			"bd address diversification is not available");
#endif

	if (chaabi_bd_address) {
		/* write to file */
		LOGD("Open file %s for writing\n", bd_addr_file_name);
		bd_addr_file = fopen(bd_addr_file_name, "w");
		if (bd_addr_file != NULL) {
			snprintf(bd_address, sizeof(bd_address), "%02X:%02X:%02X:%02X:%02X:%02X",
				chaabi_bd_address[0], chaabi_bd_address[1],
				chaabi_bd_address[2], chaabi_bd_address[3],
				chaabi_bd_address[4], chaabi_bd_address[5]);
			res = fprintf(bd_addr_file, "%s", bd_address);
			if (res)
				LOGD("BD address written successfully");
			else
				LOGE("Error %s, failed to write BD address", strerror(errno));
			fflush(bd_addr_file);
			fclose(bd_addr_file);
		}
		else {
			LOGE("Error %s while opening %s\n", strerror(errno), bd_addr_file_name);
			return errno;
		}
	} else {
		LOGE("No chaabi BD address");
	}

	return NO_ERR;
}
