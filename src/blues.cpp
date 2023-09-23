/**
 * @file blues.cpp
 * @author Bernd Giesecke (bernd@giesecke.tk)
 * @brief Blues.IO NoteCard handler
 * @version 0.1
 * @date 2023-04-27
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "main.h"
#ifndef PRODUCT_UID
#define PRODUCT_UID "com.my-company.my-name:my-project"
#endif
#define myProductID PRODUCT_UID

char blues_response[4096];

#include "blues_i2c.h"

/**
 * @brief Initialize Blues NoteCard
 *
 * @return true if NoteCard was found and setup was successful
 * @return false if NoteCard was not found or the setup failed
 */
bool init_blues(void)
{
	Wire.begin();

	// Get the ProductUID from the saved settings
	// If no settings are found, use NoteCard internal settings!
	if (read_blues_settings())
	{
		MYLOG("BLUES", "Found saved settings, override NoteCard internal settings!");
		if (memcmp(g_blues_settings.product_uid, "com.my-company.my-name", 22) == 0)
		{
			MYLOG("BLUES", "No Product ID saved");
			AT_PRINTF(":EVT NO PUID");
			memcpy(g_blues_settings.product_uid, PRODUCT_UID, 33);
		}

		MYLOG("BLUES", "Set Product ID and connection mode");
		if (blues_start_req((char *)"hub.set"))
		{
			add_string_entry((char *)"product", g_blues_settings.product_uid);
			if (g_blues_settings.conn_continous)
			{
				add_string_entry((char *)"mode", (char *)"continuous");
			}
			else
			{
				add_string_entry((char *)"mode", (char *)"minimum");
			}
			// Set sync time to 20 times the sensor read time
			add_number_entry((char *)"seconds", (g_lorawan_settings.send_repeat_time * 20 / 1000));
			add_bool_entry((char *)"heartbeat", true);

			if (!blues_send_req())
			{
				MYLOG("BLUES", "hub.set request failed");
				return false;
			}
			MYLOG("BLUES", "Response: %s", blues_response);
		}
		else
		{
			MYLOG("BLUES", "hub.set request failed");
			return false;
		}

#if USE_GNSS == 1
		MYLOG("BLUES", "Set location mode");
		if (blues_start_req((char *)"card.location.mode"))
		{
			// Continous GNSS mode
			// JAddStringToObject(req, "mode", "continous");

			// Periodic GNSS mode
			add_string_entry((char *)"mode", (char *)"periodic");

			// Set location acquisition time to the sensor read time
			add_number_entry((char *)"seconds", (g_lorawan_settings.send_repeat_time / 2000));
			add_bool_entry((char *)"heartbeat", true);
			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.location.mode request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "card.location.mode request failed");
			return false;
		}
#else
		MYLOG("BLUES", "Stop location mode");
		if (blues_start_req((char *)"card.location.mode"))
		{
			// GNSS mode off
			add_string_entry((char *)"mode", (char *)"off");
			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.location.mode request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "card.location.mode request failed");
			return false;
		}
#endif

		/// \todo reset attn signal needs rework
		// pinMode(WB_IO5, INPUT);
		// if (g_blues_settings.motion_trigger)
		// {
		// 	if (blues_start_req("card.attn"))
		// 	{
		// 		JAddStringToObject(req, "mode", "disarm");
		// 		if (!blues_send_req())
		// 		{
		// 			MYLOG("BLUES", "card.attn request failed");
		// 		}

		// 		if (!blues_enable_attn())
		// 		{
		// 			return false;
		// 		}
		// 	}
		// }
		// else
		// {
		// 	MYLOG("BLUES", "card.attn request failed");
		// 	return false;
		// }

		MYLOG("BLUES", "Set APN");
		// {“req”:”card.wireless”}
		if (blues_start_req((char *)"card.wireless"))
		{
			add_string_entry((char *)"mode", (char *)"auto");

			if (g_blues_settings.use_ext_sim)
			{
				// USING EXTERNAL SIM CARD
				add_string_entry((char *)"apn", g_blues_settings.ext_sim_apn);
				add_string_entry((char *)"method", (char *)"dual-secondary-primary");
			}
			else
			{
				// USING BLUES eSIM CARD
				add_string_entry((char *)"method", (char *)"primary");
			}
			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.wireless request failed");
				return false;
			}
		}
		else
		{
			MYLOG("BLUES", "card.wireless request failed");
			return false;
		}

#if IS_V2 == 1
		// Only for V2 cards, setup the WiFi network
		MYLOG("BLUES", "Set WiFi");
		if (blues_start_req((char *)"card.wifi"))
		{
			add_string_entry((char *)"ssid", (char *)"-");
			add_string_entry((char *)"password", (char *)"-");
			add_string_entry((char *)"name", (char *)"RAK-");
			add_string_entry((char *)"org", (char *)"RAK-PH");
			add_bool_entry((char *)"start", false);

			if (!blues_send_req())
			{
				MYLOG("BLUES", "card.wifi request failed");
			}
		}
		else
		{
			MYLOG("BLUES", "card.wifi request failed");
			return false;
		}
#endif
	}

	// {"req": "card.version"}
	if (blues_start_req((char *)"card.version"))
	{
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.version request failed");
		}
	}
	return true;
}

/**
 * @brief Send a data packet to NoteHub.IO
 *
 * @param data Payload as byte array (CayenneLPP formatted)
 * @param data_len Length of payload
 * @return true if note could be sent to NoteCard
 * @return false if note send failed
 */
bool blues_send_payload(uint8_t *data, uint16_t data_len)
{
	char payload_b86[255];

	if (blues_start_req((char *)"note.add"))
	{
		add_string_entry((char *)"file", (char *)"data.qo");
		add_bool_entry((char *)"sync", true);
		char node_id[24];
		sprintf(node_id, "%02x%02x%02x%02x%02x%02x%02x%02x",
				g_lorawan_settings.node_device_eui[0], g_lorawan_settings.node_device_eui[1],
				g_lorawan_settings.node_device_eui[2], g_lorawan_settings.node_device_eui[3],
				g_lorawan_settings.node_device_eui[4], g_lorawan_settings.node_device_eui[5],
				g_lorawan_settings.node_device_eui[6], g_lorawan_settings.node_device_eui[7]);
		add_string_nested_entry((char *)"body", (char *)"dev_eui", node_id);

		myJB64Encode(payload_b86, (const char *)data, data_len);

		add_string_entry((char *)"payload", payload_b86);

		MYLOG("BLUES", "Finished parsing");
		if (!blues_send_req())
		{
			MYLOG("BLUES", "Send request failed");
			return false;
		}
		return true;
	}
	return false;
}

/**
 * @brief Request NoteHub status, only for debug purposes
 *
 */
void blues_hub_status(void)
{
	blues_start_req((char *)"hub.status");
	blues_send_req();
}

/**
 * @brief Get the location information from the NoteCard
 *
 * @return true if a location could be acquired
 * @return false if request failed or no location is available
 */
bool blues_get_location(void)
{
	bool result = false;
	blues_start_req((char *)"card.location");
	if (blues_send_req())
	{
		if (note_resp.containsKey("lat") && note_resp.containsKey("lon"))
		{
			float blues_latitude = note_resp["lat"];
			float blues_longitude = note_resp["lon"];
			float blues_altitude = 0;

			if ((blues_latitude == 0.0) && (blues_longitude == 0.0))
			{
				MYLOG("BLUES", "No valid GPS data, report no location");
			}
			else
			{
				MYLOG("BLUES", "Got location Lat %.6f Long %0.6f", blues_latitude, blues_longitude);
				g_solution_data.addGNSS_6(LPP_CHANNEL_GPS, (uint32_t)(blues_latitude * 10000000), (uint32_t)(blues_longitude * 10000000), blues_altitude);
				result = true;
			}
		}
	}
	else
	{
		MYLOG("BLUES", "card.location failed");
	}

	if (!result)
	{
		blues_start_req((char *)"card.time");
		if (blues_send_req())
		{
			if (note_resp.containsKey("lat") && note_resp.containsKey("lon"))
			{
				float blues_latitude = note_resp["lat"];
				float blues_longitude = note_resp["lon"];
				float blues_altitude = 0;

				if ((blues_latitude == 0.0) && (blues_longitude == 0.0))
				{
					MYLOG("BLUES", "No valid GPS data, report no location");
				}
				else
				{
					MYLOG("BLUES", "Got tower location Lat %.6f Long %0.6f", blues_latitude, blues_longitude);
					g_solution_data.addGNSS_6(LPP_CHANNEL_GPS, (uint32_t)(blues_latitude * 10000000), (uint32_t)(blues_longitude * 10000000), blues_altitude);
					result = true;
				}
			}
		}
		else
		{
			MYLOG("BLUES", "card.time");
		}
	}

	// Clear last GPS location
	blues_start_req((char *)"card.location.mode");
	add_bool_entry((char *)"delete", true);
	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.location.mode delete last failed");
	}
	return result;
}

/**
 * @brief Enable ATTN interrupt
 * 		At the moment enables only the alarm on motion
 *
 * @return true if ATTN could be enabled
 * @return false if ATTN could not be enabled
 */
bool blues_enable_attn(void)
{
	// MYLOG("BLUES", "Enable ATTN on motion");
	// if (blues_start_req("card.attn"))
	// {
	// 	JAddStringToObject(req, "mode", "motion");
	// 	if (!blues_send_req())
	// 	{
	// 		MYLOG("BLUES", "card.attn request failed");
	// 		return false;
	// 	}
	// }
	// else
	// {
	// 	MYLOG("BLUES", "Request creation failed");
	// }
	// attachInterrupt(WB_IO5, blues_attn_cb, RISING);

	// MYLOG("BLUES", "Arm ATTN on motion");
	// if (blues_start_req("card.attn"))
	// {
	// 	JAddStringToObject(req, "mode", "arm");
	// 	if (!blues_send_req())
	// 	{
	// 		MYLOG("BLUES", "card.attn request failed");
	// 		return false;
	// 	}
	// }
	// else
	// {
	// 	MYLOG("BLUES", "Request creation failed");
	// }
	return true;
}

/**
 * @brief Disable ATTN interrupt
 *
 * @return true if ATTN could be disabled
 * @return false if ATTN could not be disabled
 */
bool blues_disable_attn(void)
{
	// MYLOG("BLUES", "Disable ATTN on motion");
	// detachInterrupt(WB_IO5);

	// if (blues_start_req("card.attn"))
	// {
	// 	JAddStringToObject(req, "mode", "disarm");
	// 	if (!blues_send_req())
	// 	{
	// 		MYLOG("BLUES", "card.attn request failed");
	// 	}
	// }
	// else
	// {
	// 	MYLOG("BLUES", "Request creation failed");
	// }
	// if (blues_start_req("card.attn"))
	// {
	// 	JAddStringToObject(req, "mode", "-motion");
	// 	if (!blues_send_req())
	// 	{
	// 		MYLOG("BLUES", "card.attn request failed");
	// 	}
	// }
	// else
	// {
	// 	MYLOG("BLUES", "Request creation failed");
	// }

	return true;
}

/**
 * @brief Get the reason for the ATTN interrup
 *  /// \todo work in progress
 * @return String reason /// \todo return value not final yet
 */
String blues_attn_reason(void)
{
	return "";
}

/**
 * @brief Callback for ATTN interrupt
 *       Wakes up the app_handler with an BLUES_ATTN event
 *
 */
void blues_attn_cb(void)
{
	api_wake_loop(BLUES_ATTN);
}