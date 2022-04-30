/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <zephyr.h>
#include <sys/byteorder.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include <settings/settings.h>
#include <logging/log.h>
#include <inttypes.h>


#define LOG_MODULE_NAME BGM_CENTRAL

LOG_MODULE_REGISTER(LOG_MODULE_NAME);

// #define BT_UUID_BGM_SERVICE BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x0000fee0, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))

// #define BT_UUID_BGM_PCL BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x0000fee1, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))  

// #define BT_UUID_BGM_NOTIFY BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x0000fee2, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)) 

// #define BT_UUID_BGM_WRITE BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x0000fee3, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb))  //handle 52

//BGM uuid
#define BT_UUID_BGM_SERVICE BT_UUID_DECLARE_16(0xfee0)  
#define BT_UUID_BGM_PCL     BT_UUID_DECLARE_16(0xfee1) 
#define BT_UUID_BGM_NOTIFY  BT_UUID_DECLARE_16(0xfee2) 
#define BT_UUID_BGM_WRITE   BT_UUID_DECLARE_16(0xfee3) 

//BGM handle
#define BT_HANDLE_BGM_PCL    45
#define BT_HANDLE_BGM_NOTIFY 48
#define BT_HANDLE_BGM_WRITE  52

//BGM Response code

#define BGM_FW_VERSION     0x01  //0xfe
#define BGM_ONE_RECODE     0x02  //0x9e
#define BGM_EIGHT_RECORD   0x08  //0x9d
#define BGM_SERIAl_NUMBER  0x0b  //0x37


#define MAX_EIGHT_DATA 132

enum BGM_Marker
{
	BEFORE_MEAL,
	AFTER_MEAL,
	NO_MEAL,
	MOON_MEAL,
	BEDTIME_MEAL,
	SPORT_MEAL,
	WAKEUP_MEAL,
};


enum BGM_RESP_IDX
{
	RESP_CODE = 0,
	RESP_DATA_IDX = 1,
	IND_L = 4,
	IND_H,
	DA_0,
	DA_1,
	DA_2,
	DA_3,
	DA_4,
	DA_5,
};


static struct bt_conn* default_conn;
static struct bt_uuid_16 uuid = BT_UUID_INIT_16(0);
static bool paired = false;



uint8_t eight_records[MAX_EIGHT_DATA];  //堆疊的陣列宣告
int top = -1;
bool get_eight_records = false;


static uint8_t discover_cb(struct bt_conn* conn, const struct bt_gatt_attr* attr,
	struct bt_gatt_discover_params* params);

static uint8_t read_cb(struct bt_conn* conn, uint8_t err,
	struct bt_gatt_read_params* params, const void* data,
	uint16_t length);

static void write_cb(struct bt_conn* conn, uint8_t err,
	struct bt_gatt_write_params* params);

static uint8_t notify_cb(struct bt_conn* conn,
	struct bt_gatt_subscribe_params* params,
	const void* data, uint16_t length);



struct bt_gatt_discover_params discover_params = {
	.func = discover_cb,
	.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
	.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
	.type = BT_GATT_DISCOVER_PRIMARY
};

struct bt_gatt_read_params read_params = {
	.func = read_cb,
	.by_uuid.uuid = BT_UUID_BGM_PCL,
	.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
	.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE
};

static struct bt_gatt_subscribe_params subscribe_params;



void print_resp_str(char desc[], void* data, uint16_t length)
{
	memcpy(desc, data, length);
	desc[length - 1] = '\0';
	LOG_INF("Output Description: %s", log_strdup(desc));
}

void print_resp_bytes(const void* pt, const uint8_t length, uint16_t data_length)
{
	union
	{
		uint8_t  data8;
		uint16_t data16;
		uint32_t data32;
	}u32;
	const unsigned char* u8ptr = pt;


	for (uint8_t k = 0;k < data_length;k++) {
		switch (length) {
		case 16:
			memcpy(&u32.data16, u8ptr, sizeof(u32.data16));
			printk("0x%04"PRIx16" ", u32.data16);
			u8ptr += sizeof(u32.data16);
			break;
		case 32:
			memcpy(&u32.data32, u8ptr, sizeof(u32.data32));
			printk("0x%08"PRIx32" ", u32.data32);
			u8ptr += sizeof(u32.data32);
			break;
		default:
			printk("0x%02"PRIx8" ", *u8ptr);
			u8ptr++;
			break;
		}
	}

	printk("\n\n");

}

void bgm_marker_to_str(const uint16_t marker, char* str, size_t len) {

	switch (marker) {
	case BEFORE_MEAL:
		snprintk(str, len, "%s", "Befoare Meal");
		break;
	case AFTER_MEAL:
		snprintk(str, len, "%s", "After Meal");
		break;
	case NO_MEAL:
		snprintk(str, len, "%s", "No Meal");
		break;
	case MOON_MEAL:
		snprintk(str, len, "%s", "Moon Meal");
		break;
	case BEDTIME_MEAL:
		snprintk(str, len, "%s", "BedTime Meal");
		break;
	case SPORT_MEAL:
		snprintk(str, len, "%s", "Sport Meal");
		break;
	case WAKEUP_MEAL:
		snprintk(str, len, "%s", "Wakeup Meal");
		break;
	default:
		(void)memset(str, 0, len);
		return;
	}
}

void bgm_timezon_to_str(const uint16_t timezone, char* str, size_t len) {

	if (timezone > 0xC){
		snprintk(str, len, "-%u", (timezone - 0xC));
		
	}else{
		snprintk(str, len, "+%u", (0xC - timezone)) ;
	}
}
void push(uint8_t* arr, uint8_t data) {
	if (top >= MAX_EIGHT_DATA) {
		LOG_ERR("Can't push to array.");
	}
	else {
		top++;
		arr[top] = data;
	}
}

static uint8_t discover_cb(struct bt_conn* conn, const struct bt_gatt_attr* attr,
	struct bt_gatt_discover_params* params)
{
	// char dest[150];
	// bt_uuid_to_str(attr->uuid, dest, sizeof(dest));

	// LOG_INF("discover cb attr->uuid:  %s handle %d",log_strdup(dest),attr->handle);

	int err;

	if (!attr) {
		LOG_INF("Discover complete");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	LOG_INF("[ATTRIBUTE] handle %u\n", attr->handle);


	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_BGM_SERVICE)) {
		memcpy(&uuid, BT_UUID_BGM_NOTIFY, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	}
	else if (!bt_uuid_cmp(discover_params.uuid,
		BT_UUID_BGM_NOTIFY)) {
		memcpy(&uuid, BT_UUID_GATT_CCC, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discover failed (err %d)\n", err);
		}
	}
	else {
		subscribe_params.notify = notify_cb;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY) {
			LOG_ERR("Subscribe failed (err %d)\n", err);
		}
		else {
			LOG_INF("[SUBSCRIBED]");

			if (paired) {

				//Read One Record Type 1(read amount of blood)
				static uint8_t type1_data[] = { 0xB0,0x61,0x00,0x00,0x11 };
				static struct bt_gatt_write_params write_op;
				write_op = (struct bt_gatt_write_params){
					.handle = BT_HANDLE_BGM_WRITE,
					.offset = 0,
					.data = &type1_data,
					.length = sizeof(type1_data),
					.func = write_cb,
				};
				err = bt_gatt_write(conn, &write_op);
				if (err) {
					printk("Write request failed (%d)\n", err);
				}


				// //Read One Record Type2 ( read specific blood glucose)
				// static uint8_t type2_data[] = { 0xB0,0x61,0x07,0x00,0x18 };

				// write_op.data = &type2_data;
				// write_op.length = sizeof(type2_data);

				// err = bt_gatt_write(conn, &write_op);
				// if (err) {
				// 	printk("Write request failed (%d)\n", err);
				// }



				//Read Eight Record
				static uint8_t eight_recode_data[] = { 0xB0,0x62,0x08,0x00,0x1A };


				write_op.data = &eight_recode_data;
				write_op.length = sizeof(eight_recode_data);

				err = bt_gatt_write(conn, &write_op);
				if (err) {
					printk("Write request failed (%d)\n", err);
				}

				//only get once value
				paired = false;
			}

			//If the write function is here, it will be set to cyclic write

		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;

}

static uint8_t read_cb(struct bt_conn* conn, uint8_t err,
	struct bt_gatt_read_params* params, const void* data,
	uint16_t length)
{

	struct bt_conn_info info;
	char dest[150];
	bt_uuid_to_str(params->by_uuid.uuid, dest, sizeof(dest));

	LOG_INF("Read cb- uuid: %s handle %d", log_strdup(dest), params->single.handle);

	/* read complete */
	if (!data) {
		return BT_GATT_ITER_STOP;
	}


	char desc[BT_ATT_MAX_ATTRIBUTE_LEN + 1];
	if (data) {

		if (length > sizeof(desc) - 1) {
			LOG_INF("Description truncated from %u to %zu octets",
				length, sizeof(desc) - 1);
		}
		length = MIN(sizeof(desc) - 1, length);

		/* TODO: Handle long reads */
		memcpy(desc, data, length);
	}

	desc[length] = '\0';
	LOG_INF("Output description: %s", log_strdup(desc));

	bt_conn_get_info(conn, &info);
	LOG_INF("Role %u", info.role);

	// return BT_GATT_ITER_CONTINUE;
	return BT_GATT_ITER_STOP;
}

void write_cb(struct bt_conn* conn, uint8_t err,
	struct bt_gatt_write_params* params)
{
	if (err) {
		LOG_WRN("Write request completed with BT_ATT_ERR 0x%02x\n", err);
		LOG_WRN("Write request completed with BT_ATT_ERR %d\n", err);
	}
	else {
		LOG_INF("Write Sucessfull handle %u data length:%d", params->handle, params->length);
	}

}

static uint8_t notify_cb(struct bt_conn* conn,
	struct bt_gatt_subscribe_params* params,
	const void* data, uint16_t length)
{

	if (!data) {
		LOG_INF("[UNSUBSCRIBED]");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	LOG_INF("[NOTIFICATION] data length %u  handle %u", length, params->value_handle);
	char desc[BT_ATT_MAX_ATTRIBUTE_LEN + 1];


	if (data) {

		if (length > sizeof(desc) - 1) {
			LOG_INF("Description truncated from %u to %zu octets",
				length, sizeof(desc) - 1);
		}
		length = MIN(sizeof(desc) - 1, length);

		LOG_INF("Length :%d ,Desc %d", length, sizeof(desc));

		//convert data to uint8 arrays
		uint8_t resp[length];
		memcpy(resp, data, length);




		// print_resp_bytes(data,8,length);

		switch (resp[RESP_CODE]) {
		case BGM_ONE_RECODE:
		{
			uint8_t record_idx = (resp[IND_H] << 8) + resp[IND_L];
			LOG_INF("Record_idx : 0x%02x", record_idx);

			// parser fisrt response data
			if (record_idx > 0 & resp[RESP_DATA_IDX] == 1) {

				uint16_t year = (resp[DA_3] & 0x7F) + 0x7D0;
				uint16_t month = ((resp[DA_1] & 0xC0) >> 4) + ((resp[DA_0] & 0xC0) >> 6) + 1;
				uint16_t day = (resp[DA_0] & 0x1F) + 1;
				uint16_t hour = resp[DA_1] & 0x1F;
				uint16_t minute = resp[DA_2] & 0x3F;
				uint16_t glucose = ((resp[DA_4] & 0x03) << 8) + resp[DA_5];
				uint16_t timezone = ((resp[DA_1] & 0x20) >> 5) + ((resp[DA_2] & 0xC0) >> 5) + ((resp[DA_4] & 0xC0) >> 3);
				uint16_t marker = ((resp[DA_4] & 0x38) >> 3);
				char tz[10];
				bgm_timezon_to_str(timezone,tz,sizeof(tz));


				char dest[20];
				bgm_marker_to_str(marker, dest, sizeof(dest));
				printk("%s Glucose:%u (%u/%u/%u %u:%u GMT %s)\n", dest, glucose, year, month, day, hour, minute,tz);
			}
			else if (resp[RESP_DATA_IDX] == 1) {
				uint16_t total_amount = (resp[DA_1] << 8) + resp[DA_0];
				uint16_t last_transfer = (resp[DA_5] << 8) + resp[DA_4];
				uint16_t max_amount = (resp[DA_3] << 8) + resp[DA_2];
				LOG_INF("Total Amount : 0x%03x", total_amount);
				LOG_INF("Last Transfer : 0x%03x", last_transfer);
				LOG_INF("Max Amount : %u", max_amount);
			}
			break;

		}
		//entry it will merge bytes array 
		case BGM_EIGHT_RECORD:
		{
			int idx = 0;

			if (resp[RESP_DATA_IDX] == 7) {
				get_eight_records = true;
			}
			if (resp[RESP_DATA_IDX] != 1) {
				idx = 2;
			}

			for (idx;idx < length;idx++) {
				push(&eight_records, resp[idx]);
			}

			break;
		}

		default:
		{
			memcpy(desc, data, length);
			desc[length - 1] = '\0';
			LOG_INF("Output Description: %s", log_strdup(desc));
		}


		}

		//print eight records
		if (get_eight_records) {
			for (int i = 0;i < 8;i++) {
				int add_idx = 16 * i;
				uint16_t year = (eight_records[DA_3 + add_idx] & 0x7F) + 0x7D0;
				uint16_t month = ((eight_records[DA_1 + add_idx] & 0xC0) >> 4) + ((eight_records[DA_0 + add_idx] & 0xC0) >> 6) + 1;
				uint16_t day = (eight_records[DA_0 + add_idx] & 0x1F) + 1;
				uint16_t hour = eight_records[DA_1 + add_idx] & 0x1F;
				uint16_t minute = eight_records[DA_2 + add_idx] & 0x3F;
				uint16_t glucose = ((eight_records[DA_4 + add_idx] & 0x03) << 8) + eight_records[DA_5 + add_idx];
				uint16_t timezone = ((eight_records[DA_1 + add_idx] & 0x20) >> 5) + ((eight_records[DA_2 + add_idx] & 0xC0) >> 5) + ((eight_records[DA_4 + add_idx] & 0xC0) >> 3);
				uint16_t marker = ((eight_records[DA_4 + add_idx] & 0x38) >> 3);
				
				char tz[10];
				bgm_timezon_to_str(timezone,tz,sizeof(tz));


				char dest[20];
				bgm_marker_to_str(marker, dest, sizeof(dest));
				printk("%s Glucose:%u (%u/%u/%u %u:%u GMT %s)\n", dest, glucose, year, month, day, hour, minute,tz);
			}
			
			get_eight_records = false;
			top = -1;
			memset(eight_records, 0, MAX_EIGHT_DATA);
		}

	}

	return BT_GATT_ITER_CONTINUE;
}


static void bt_ready(int err)
{

	// k_busy_wait(10 * 1000); // DB hash delayed work is 10 MS
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
}

static void connected(struct bt_conn* conn, uint8_t conn_err)
{

	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_INF("Failed to connect to %s (%d)", log_strdup(addr),
			conn_err);

		if (default_conn == conn) {
			default_conn = NULL;

			err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
			if (err) {
				LOG_ERR("Scanning failed to start (err %d)",
					err);
			}
		}
		//if connection faild, it will return to restart scan function/
		return;
	}

	LOG_INF("Connected : %s", log_strdup(addr));


	err = bt_conn_set_security(conn, BT_SECURITY_L1);
	if (err) {
		LOG_WRN("Failed to set security: %d", err);
		bt_gatt_discover(conn, &discover_params);
	}

	//get bgm model nam
	// err = bt_gatt_read(conn, &read_params);
	// if (err) {
	// 	LOG_WRN("Failed to read : %d", err);
	// }

	err = bt_scan_stop();
	if ((!err) && (err != -EALREADY)) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
	}
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason %u)", log_strdup(addr),
		reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)",
			err);
	}
}

static void security_changed(struct bt_conn* conn, bt_security_t level,
	enum bt_security_err err)
{


	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", log_strdup(addr),
			level);
	}
	else {
		LOG_WRN("Security failed: %s level %u err %d", log_strdup(addr),
			level, err);
	}

	if (conn == default_conn) {

		if (paired) {
			//Open pcl mode
			static uint8_t data[] = { 0x00 };
			static struct bt_gatt_write_params write_op;
			write_op = (struct bt_gatt_write_params){
				.handle = BT_HANDLE_BGM_PCL,
				.offset = 0,
				.data = &data,
				.length = sizeof(data),
				.func = write_cb,
			};

			err = bt_gatt_write(conn, &write_op);
			if (err) {
				LOG_ERR("Write request failed (%d)\n", err);
			}
		}

		LOG_INF("Initial discover params");

		memcpy(&uuid, BT_UUID_BGM_SERVICE, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.func = discover_cb;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(default_conn, &discover_params);
		if (err) {
			printk("Discover failed(err %d)\n", err);
			return;
		}
	}

}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed
};

static void scan_filter_match(struct bt_scan_device_info* device_info,
	struct bt_scan_filter_match* filter_match,
	bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	LOG_INF("Filters matched. Address: %s connectable: %d",
		log_strdup(addr), connectable);
}

static void scan_connecting_error(struct bt_scan_device_info* device_info)
{
	LOG_WRN("Connecting failed");
}

static void scan_connecting(struct bt_scan_device_info* device_info,
	struct bt_conn* conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	LOG_INF("Scan connecting. Address: %s", log_strdup(addr));


	default_conn = bt_conn_ref(conn);
}


BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL,
	scan_connecting_error, scan_connecting);

static int scan_init(void)
{
	int err;
	struct bt_scan_init_param scan_init = {
		.connect_if_match = 1,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	//Find BGM Service
	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_BGM_SERVICE);
	if (err) {
		LOG_ERR("Scanning filters cannot be set (err %d)", err);
		return err;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		LOG_ERR("Filters cannot be turned on (err %d)", err);
		return err;
	}

	LOG_INF("Scan module initialized");
	return err;
}


static void auth_cancel(struct bt_conn* conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", log_strdup(addr));
}

static void pairing_confirm(struct bt_conn* conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	bt_conn_auth_pairing_confirm(conn);


	LOG_INF("Pairing confirmed: %s", log_strdup(addr));


}


static void pairing_complete(struct bt_conn* conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d", log_strdup(addr),
		bonded);

	paired = true;

}


static void pairing_failed(struct bt_conn* conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_WRN("Pairing failed conn: %s, reason %d", log_strdup(addr),
		reason);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
	.pairing_confirm = pairing_confirm,
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

void main(void)
{
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Failed to register authorization callbacks.");
		return;
	}

	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_INF("Bluetooth initialized");

	scan_init();

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");

}
