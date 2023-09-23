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

bool gnss_continuous = true;

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
		bool request_success = false;

		MYLOG("BLUES", "Found saved settings, override NoteCard internal settings!");
		if (memcmp(g_blues_settings.product_uid, "com.my-company.my-name", 22) == 0)
		{
			MYLOG("BLUES", "No Product ID saved");
			AT_PRINTF(":EVT NO PUID");
			memcpy(g_blues_settings.product_uid, PRODUCT_UID, 33);
		}

		MYLOG("BLUES", "Set Product ID and connection mode");
		for (int try_send = 0; try_send < 3; try_send++)
		{
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

				if (blues_send_req())
				{
					MYLOG("BLUES", "Response: %s", blues_response);
					request_success = true;
					break;
				}
			}
			delay(100);
		}
		if (!request_success)
		{
			MYLOG("BLUES", "hub.set request failed");
			return false;
		}
		request_success = false;

		// Enable motion trigger
		for (int try_send = 0; try_send < 3; try_send++)
		{
			if (blues_start_req((char *)"card.motion.mode"))
			{
				add_bool_entry((char *)"start", true);

				// Set sensitivity
				add_number_entry((char *)"sensitivity", -1);

				if (blues_send_req())
				{
					request_success = true;
					break;
				}
				MYLOG("BLUES", "Response: %s", blues_response);
			}
			delay(100);
		}
		if (!request_success)
		{
			MYLOG("BLUES", "card.motion.mode request failed");
			return false;
		}
		request_success = false;

		// Enable GNSS continuous mode
		for (int try_send = 0; try_send < 3; try_send++)
		{
			if (blues_switch_gnss_mode(true))
			{
				request_success = true;
				break;
			}
		}
		if (!request_success)
		{
			MYLOG("BLUES", "card.location.mode request failed");
			return false;
		}
		request_success = false;

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
		for (int try_send = 0; try_send < 3; try_send++)
		{
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
				if (blues_send_req())
				{
					request_success = true;
					break;
				}
			}
		}
		if (!request_success)
		{
			MYLOG("BLUES", "card.wireless request failed");
			return false;
		}
		request_success = false;

#if IS_V2 == 1
		// Only for V2 cards, setup the WiFi network
		MYLOG("BLUES", "Set WiFi");
		for (int try_send = 0; try_send < 3; try_send++)
		{
			if (blues_start_req((char *)"card.wifi"))
			{
				add_string_entry((char *)"ssid", (char *)"-");
				add_string_entry((char *)"password", (char *)"-");
				add_string_entry((char *)"name", (char *)"RAK-");
				add_string_entry((char *)"org", (char *)"RAK-PH");
				add_bool_entry((char *)"start", false);

				if (blues_send_req())
				{
					request_success = true;
					break;
				}
			}
		}
		if (!request_success)
		{
			MYLOG("BLUES", "card.wifi request failed");
			return false;
		}
		request_success = false;
#endif
	}

	for (int try_send = 0; try_send < 3; try_send++)
	{
		if (blues_start_req((char *)"card.version"))
		{
			if (blues_send_req())
			{
				break;
			}
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
	bool request_success = false;
	for (int try_send = 0; try_send < 3; try_send++)
	{
		blues_start_req((char *)"hub.status");
		if (blues_send_req())
		{
			request_success = true;
			break;
		}
	}
	if (!request_success)
	{
		MYLOG("BLUES", "hub.status request failed");
	}
}

bool blues_switch_gnss_mode(bool continuous_on)
{
	bool request_success = false;
	if (continuous_on != gnss_continuous)
	{
		MYLOG("BLUES", "Change of GNSS mode, switch off first");
		// Enable motion trigger
		for (int try_send = 0; try_send < 3; try_send++)
		{
			if (blues_start_req((char *)"card.location.mode"))
			{
				// GNSS mode off
				add_string_entry((char *)"mode", (char *)"off");
			}
			if (blues_send_req())
			{
				request_success = true;
				break;
			}
		}
		if (!request_success)
		{
			MYLOG("BLUES", "card.location.mode request failed");
			return false;
		}
	}

	request_success = false;
	MYLOG("BLUES", "Set location mode %s", continuous_on ? "continuous" : "periodic");
	for (int try_send = 0; try_send < 3; try_send++)
	{
		if (blues_start_req((char *)"card.location.mode"))
		{
			if (continuous_on)
			{
				gnss_continuous = true;
				// Continous GNSS mode
				add_string_entry((char *)"mode", (char *)"continuous");
				add_number_entry((char *)"threshold", 1);
			}
			else
			{
				gnss_continuous = false;
				// Periodic GNSS mode
				add_string_entry((char *)"mode", (char *)"periodic");
				add_number_entry((char *)"threshold", 1);
			}
			// Set location acquisition time to the sensor read time
			add_number_entry((char *)"seconds", (g_lorawan_settings.send_repeat_time / 2000));
			if (blues_send_req())
			{
				request_success = true;
				break;
			}
		}
	}
	if (!request_success)
	{
		MYLOG("BLUES", "card.location.mode request failed");
		return false;
	}
	return true;
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
	bool got_gnss_location = false;
	bool request_success = false;

	for (int try_send = 0; try_send < 3; try_send++)
	{
		blues_start_req((char *)"card.location");
		if (blues_send_req())
		{
			if (note_resp.containsKey("lat") && note_resp.containsKey("lon"))
			{
				float blues_latitude = note_resp["lat"];
				float blues_longitude = note_resp["lon"];
				float blues_altitude = 0;
				got_gnss_location = true;

				if ((blues_latitude == 0.0) && (blues_longitude == 0.0))
				{
					MYLOG("BLUES", "No valid GPS data, report no location");
				}
				else
				{
					MYLOG("BLUES", "Got location Lat %.6f Long %0.6f", blues_latitude, blues_longitude);
					g_solution_data.addGNSS_6(LPP_CHANNEL_GPS, (uint32_t)(blues_latitude * 10000000), (uint32_t)(blues_longitude * 10000000), blues_altitude);
					g_solution_data.addPresence(LPP_CHANNEL_GPS_TOWER, false);
					if (gnss_continuous)
					{
						// We got a location, switch to periodic mode
						blues_switch_gnss_mode(false);
					}
					result = true;
				}
			}
			request_success = true;
			break;
		}
	}
	if (!request_success)
	{
		MYLOG("BLUES", "card.location request failed");
	}

	request_success = false;
	if (!result)
	{
		if (!gnss_continuous)
		{
			// Switch GNSS to continuous to get a location
			blues_switch_gnss_mode(true);
		}
	}

	for (int try_send = 0; try_send < 3; try_send++)
	{
		blues_start_req((char *)"card.time");
		if (blues_send_req())
		{
			if (note_resp.containsKey("lat") && note_resp.containsKey("lon"))
			{
				if (note_resp.containsKey("country"))
				{
					char *country = (char *)note_resp["country"].as<const char *>();
					// Try to set LoRaWAN band automatically
					if (strcmp(country, "PH") == 0)
					{
						MYLOG("BLUES", "Found PH, use band 10");
					}
					else if (strcmp(country, "JP") == 0)
					{
						MYLOG("BLUES", "Found JP, use band 8");
					}
					else if (strcmp(country, "US") == 0)
					{
						MYLOG("BLUES", "Found US, use band 5");
					}
					else if (strcmp(country, "AU") == 0)
					{
						MYLOG("BLUES", "Found AU, use band 6");
					}
					else if (strcmp(country, "DE") == 0)
					{
						MYLOG("BLUES", "Found JP, use band 8");
					}
				}

				if (!got_gnss_location)
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
						g_solution_data.addPresence(LPP_CHANNEL_GPS_TOWER, true);
						result = true;
					}
				}
			}
			request_success = true;
			break;
		}
	}
	if (!request_success)
	{
		MYLOG("BLUES", "card.time request failed");
	}

	if (got_gnss_location)
	{
		request_success = false;
		MYLOG("BLUES", "card.location.mode delete last location");
		// Clear last GPS location
		for (int try_send = 0; try_send < 3; try_send++)
		{
			blues_start_req((char *)"card.location.mode");
			add_bool_entry((char *)"delete", true);
			if (blues_send_req())
			{
				request_success = true;
				break;
			}
		}
		if (!request_success)
		{
			MYLOG("BLUES", "card.location.mode delete last location request failed");
		}
	}
	return result;
}

void blues_card_restore(void)
{
	blues_start_req((char *)"hub.status");
	add_bool_entry((char *)"delete", true);
	add_bool_entry((char *)"connected", true);
	blues_send_req();
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