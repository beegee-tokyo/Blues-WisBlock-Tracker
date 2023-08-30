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

Notecard notecard;

void blues_attn_cb(void);

J *req;

/**
 * @brief Initialize Blues NoteCard
 *
 * @return true if NoteCard was found and setup was successful
 * @return false if NoteCard was not found or the setup failed
 */
bool init_blues(void)
{
	Wire.begin();
	notecard.begin();

	// Get the ProductUID from the saved settings
	read_blues_settings();

	if (memcmp(g_blues_settings.product_uid, "com.my-company.my-name", 22) == 0)
	{
		MYLOG("BLUES", "No Product ID saved");
		AT_PRINTF(":EVT NO PUID");
		memcpy(g_blues_settings.product_uid, PRODUCT_UID, 33);
	}

	MYLOG("BLUES", "Set Product ID and connection mode");
	req = notecard.newRequest("hub.set");

	JAddStringToObject(req, "product", g_blues_settings.product_uid);
	if (g_blues_settings.conn_continous)
	{
		JAddStringToObject(req, "mode", "continuous");
	}
	else
	{
		JAddStringToObject(req, "mode", "periodic");
	}

	// JAddNumberToObject(req, "seconds", 300);
	// Set sync time to 20 times the sensor read time
	JAddNumberToObject(req, "seconds", (g_lorawan_settings.send_repeat_time * 20 / 1000));
	JAddBoolToObject(req, "heartbeat", true);

	if (!blues_send_req())
	{
		MYLOG("BLUES", "hub.set request failed");
		return false;
	}

#if USE_GNSS == 1
	MYLOG("BLUES", "Set location mode");
	req = notecard.newRequest("card.location.mode");

	// Continous GNSS mode
	// JAddStringToObject(req, "mode", "continous");

	// Periodic GNSS mode
	JAddStringToObject(req, "mode", "periodic");

	// Set location acquisition time to the sensor read time
	JAddNumberToObject(req, "seconds", (g_lorawan_settings.send_repeat_time / 2000));
	JAddBoolToObject(req, "heartbeat", true);
	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.location.mode request failed");
		return false;
	}

	MYLOG("BLUES", "Enable location triangulation");
	req = notecard.newRequest("card.triangulate");

	JAddStringToObject(req, "mode", "cell");
	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.triangulate request failed");
		return false;
	}

	if (g_blues_settings.motion_trigger)
	{
		pinMode(WB_IO5, INPUT);

		req = notecard.newRequest("card.attn");

		JAddStringToObject(req, "mode", "disarm");
		if (!blues_send_req())
		{
			MYLOG("BLUES", "card.attn request failed");
		}

		if (!blues_enable_attn())
		{
			return false;
		}
	}
#endif

	/*************************************************/
	/* If the Notecard is properly setup, there is   */
	/* need to setup the APN and card mode on every  */
	/* restart! It will reuse the APN and mode that  */
	/* was originally setup.                         */
	/*************************************************/
	MYLOG("BLUES", "Set APN");
	// {“req”:”card.wireless”}
	req = notecard.newRequest("card.wireless");
	JAddStringToObject(req, "mode", "auto");

	if (g_blues_settings.use_ext_sim)
	{
		// USING EXTERNAL SIM CARD
		JAddStringToObject(req, "apn", g_blues_settings.ext_sim_apn);
		JAddStringToObject(req, "method", "secondary");
	}
	else
	{
		// USING BLUES eSIM CARD
		JAddStringToObject(req, "method", "primary");
	}
	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.wireless request failed");
		return false;
	}

#if IS_V2 == 1
	// Only for V2 cards, setup the WiFi network
	MYLOG("BLUES", "Set WiFi");
	req = notecard.newRequest("card.wifi");

	JAddStringToObject(req, "ssid", "-");
	JAddStringToObject(req, "password", "-");
	JAddStringToObject(req, "name", "RAK-");
	JAddStringToObject(req, "org", "RAK-PH");
	JAddBoolToObject(req, "start", false);

	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.wifi request failed");
	}
#endif
	/*************************************************/
	/* End of code block to be removed               */
	/*************************************************/

	return true;
}

/**
 * @brief Create a request structure to be sent to the NoteCard
 *
 * @param request_name name of request, e.g. card.wireless
 * @return true if request could be created
 * @return false if request could not be created
 */
bool blues_start_req(String request_name)
{
	req = notecard.newRequest(request_name.c_str());
	if (req != NULL)
	{
		return true;
	}
	return false;
}

/**
 * @brief Send a completed request to the NoteCard.
 *
 * @return true if request could be sent and the response does not have "err"
 * @return false if request could not be sent or the response did have "err"
 */
bool blues_send_req(void)
{
	char *json = JPrintUnformatted(req);
	MYLOG("BLUES", "Card request = %s", json);

	J *rsp;
	rsp = notecard.requestAndResponse(req);
	if (rsp == NULL)
	{
		return false;
	}
	json = JPrintUnformatted(rsp);
	if (JIsPresent(rsp, "err"))
	{
		MYLOG("BLUES", "Card error response = %s", json);
		notecard.deleteResponse(rsp);
		return false;
	}
	MYLOG("BLUES", "Card response = %s", json);
	notecard.deleteResponse(rsp);

	return true;
}

/**
 * @brief Request NoteHub status, mainly for debug purposes
 *
 */
void blues_hub_status(void)
{
	blues_start_req("hub.status");
	if (!blues_send_req())
	{
		MYLOG("BLUES", "hub.status request failed");
	}
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
	if (blues_start_req("card.location"))
	{
		J *rsp;
		rsp = notecard.requestAndResponse(req);
		if (rsp == NULL)
		{
			MYLOG("BLUES", "card.location failed, report no location");
			return false;
		}
		char *json = JPrintUnformatted(rsp);
		MYLOG("BLUES", "Card response = %s", json);

		if (JHasObjectItem(rsp, "lat") && JHasObjectItem(rsp, "lat"))
		{
			float blues_latitude = JGetNumber(rsp, "lat");
			float blues_longitude = JGetNumber(rsp, "lon");
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

		notecard.deleteResponse(rsp);
	}

	if (!result)
	{
		// No GPS coordinates, get last tower location
		if (blues_start_req("card.time"))
		{
			J *rsp;
			rsp = notecard.requestAndResponse(req);
			if (rsp == NULL)
			{
				MYLOG("BLUES", "card.time failed, report no location");
				return false;
			}
			char *json = JPrintUnformatted(rsp);
			MYLOG("BLUES", "Card response = %s", json);

			if (JHasObjectItem(rsp, "lat") && JHasObjectItem(rsp, "lat"))
			{
				float blues_latitude = JGetNumber(rsp, "lat");
				float blues_longitude = JGetNumber(rsp, "lon");
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

			notecard.deleteResponse(rsp);
		}
	}

	// Clear last GPS location
	if (blues_start_req("card.location.mode"))
	{
		notecard.sendRequest(req);
		JAddBoolToObject(req, "delete", true);
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
	MYLOG("BLUES", "Enable ATTN on motion");
	req = notecard.newRequest("card.attn");

	JAddStringToObject(req, "mode", "motion");
	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.attn request failed");
		return false;
	}
	attachInterrupt(WB_IO5, blues_attn_cb, RISING);

	MYLOG("BLUES", "Arm ATTN on motion");
	req = notecard.newRequest("card.attn");

	JAddStringToObject(req, "mode", "arm");
	if (!blues_send_req())
	{
		MYLOG("BLUES", "card.attn request failed");
		return false;
	}
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